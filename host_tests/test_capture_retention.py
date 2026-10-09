"""Ownership and failure preservation for optional USB capture diagnostics."""
import json
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest
import wave

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools/voice'))
from export_audio_clip import export_clip, retain_turn_clip


class Device:
    def __init__(self, *, busy=False, corrupt=False, resume_error=False):
        self.link=SimpleNamespace(timeout=.2)
        self.enabled=True
        self.calls=[]
        self.busy=busy
        self.corrupt=corrupt
        self.resume_error=resume_error

    def command(self, command, **kwargs):
        self.calls.append(command)
        if command=='agent status':return dict(busy=self.busy)
        if command=='agent voice status':return dict(voice_enabled=True,mode='fast',preconnect='idle')
        if command=='agent wake status':return dict(enabled=self.enabled,state='listening' if self.enabled else 'off')
        if command=='agent wake off':self.enabled=False;return {}
        if command=='agent wake on':
            if self.resume_error:raise RuntimeError('resume failed')
            self.enabled=True;return {}
        if command=='agent audio status':return dict(clip_ready=True,recording=False,clip_ms=20)
        assert command.startswith('agent audio clip read ') and not self.enabled
        _,_,_,_,offset,count=command.split()
        return dict(offset=int(offset)+(1 if self.corrupt else 0),pcm='0100'*int(count))


class RetentionTests(unittest.TestCase):
    def test_verified_export_precedes_wake_resume_without_voice_reset(self):
        with tempfile.TemporaryDirectory() as t:
            p=Path(t)/'clip';d=Device();result=retain_turn_clip(d,p)
            self.assertTrue(result['complete'] and result['listener_restored'] and d.enabled)
            self.assertEqual(d.link.timeout,.2)
            self.assertFalse(any(c in ('agent voice off','agent voice on') for c in d.calls))
            self.assertLess(d.calls.index('agent audio clip read 256 64'),d.calls.index('agent wake on'))
            with wave.open(str(p/'device-clip.wav'),'rb') as w:
                self.assertEqual(w.getnframes(),320)
                self.assertEqual(w.readframes(320),b'\x01\0'*320)
            self.assertTrue(json.loads((p/'retention.json').read_text())['complete'])

    def test_corruption_keeps_failed_clip_protected(self):
        with tempfile.TemporaryDirectory() as t:
            p=Path(t)/'clip';d=Device(corrupt=True)
            with self.assertRaisesRegex(RuntimeError,'Invalid clip response'):retain_turn_clip(d,p)
            self.assertFalse(d.enabled)
            self.assertNotIn('agent wake on',d.calls)
            self.assertEqual(d.link.timeout,.2)
            self.assertFalse((p/'device-clip.wav').exists())
            self.assertFalse(json.loads((p/'retention.json').read_text())['complete'])

    def test_busy_refused_before_any_listener_mutation(self):
        with tempfile.TemporaryDirectory() as t:
            d=Device(busy=True)
            with self.assertRaisesRegex(RuntimeError,'idle active'):retain_turn_clip(d,Path(t)/'clip')
            self.assertTrue(d.enabled)
            self.assertNotIn('agent wake off',d.calls)
            self.assertEqual(d.link.timeout,.2)

    def test_replaced_capture_not_mislabelled_as_current_failure(self):
        with tempfile.TemporaryDirectory() as t:
            d=Device()
            with self.assertRaisesRegex(RuntimeError,'replaced before retention'):
                retain_turn_clip(d,Path(t)/'clip',expected_wake=dict(record_at=123,end_at=145,samples=320,completed=1))
            self.assertTrue(d.enabled)
            self.assertNotIn('agent wake off',d.calls)

    def test_resume_failure_does_not_erase_export(self):
        with tempfile.TemporaryDirectory() as t:
            p=Path(t)/'clip';d=Device(resume_error=True)
            with self.assertRaisesRegex(RuntimeError,'resume failed'):retain_turn_clip(d,p)
            self.assertTrue((p/'device-clip.wav').exists())
            r=json.loads((p/'retention.json').read_text())
            self.assertTrue(r['clip']['complete'])
            self.assertFalse(r['complete'] or r['listener_restored'])

    def test_expired_budget_and_existing_directory_not_overwritten(self):
        with tempfile.TemporaryDirectory() as t:
            p=Path(t);d=Device()
            with self.assertRaises(TimeoutError):export_clip(d,p,create=False,deadline=-1)
            self.assertFalse(d.calls)
            with self.assertRaises(FileExistsError):retain_turn_clip(d,p)
            self.assertFalse(d.calls)


if __name__=='__main__':unittest.main()
