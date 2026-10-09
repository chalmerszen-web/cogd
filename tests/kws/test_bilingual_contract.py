"""Replay integration against a real 0.10.0 JSON contract, with hardware mocked."""
import copy
import hashlib
import json
from pathlib import Path
import struct
import sys
from types import SimpleNamespace
import wave

import pytest

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools/kws'))
import acoustic_test
import bilingual_replay
from calibrate_acoustic import export_clip

FIXTURE=json.loads((Path(__file__).parent/'fixtures/voice_flow_runtime.json').read_text(encoding='utf8'))


class Runtime:
    def __init__(self):
        self.wake=copy.deepcopy(FIXTURE['wake'])
        self.voice=copy.deepcopy(FIXTURE['voice'])
        self.audio=copy.deepcopy(FIXTURE['audio'])
        self.calls=[]
        self.clock=0.
        self.clip=b''.join(struct.pack('<h',(i%200)-100) for i in range(self.audio['clip_ms']*16))

    def command(self,text,query=False,timeout=30):
        self.calls.append((text,query));self.clock+=.2
        expected=text.endswith(' status') or text=='agent kws profile' or text.startswith('agent audio clip read ')
        assert query==expected,text
        if text=='agent wake status':return copy.deepcopy(self.wake)
        if text=='agent voice status':return copy.deepcopy(self.voice)
        if text=='agent audio status':return copy.deepcopy(self.audio)
        if text=='agent status':return copy.deepcopy(FIXTURE['status'])
        if text=='agent kws profile':
            histogram=[0]*64;histogram[16]=640
            return dict(trained=True,blocks=640,max_us=8048,bin_us=500,histogram=histogram)
        if text.startswith('agent audio clip read '):
            offset,count=map(int,text.split()[-2:]);assert 0<count<=256
            return dict(offset=offset,pcm=self.clip[offset*2:(offset+count)*2].hex())
        if text=='agent wake off':self.wake.update(enabled=False,state='off',chunk=0)
        elif text=='agent wake on':self.wake.update(enabled=True,state='listening',chunk=512)
        elif text=='agent voice on':
            self.voice.update(voice_enabled=True,stage='listening');self.wake.update(enabled=True,state='listening',chunk=512)
        elif text=='agent voice off':self.voice.update(voice_enabled=False,stage='off')
        elif text.startswith('agent wake threshold '):self.wake['threshold']=int(text.split()[-1])
        elif text.startswith('agent wake gain '):
            assert not self.wake['enabled'];self.wake['input_gain']=int(text.split()[-1])
        elif text in ('agent cancel','agent kws reset-profile'):pass
        else:raise AssertionError('Unexpected command '+text)
        return {'lines':['@ok ok']}


@pytest.mark.parametrize('voice_enabled,wake_enabled,input_gain', [(True,True,1),(False,True,3),(False,False,2)])
def test_restore_real_status_input_gain_and_enable_modes(voice_enabled,wake_enabled,input_gain):
    runtime=Runtime();original=copy.deepcopy(FIXTURE['wake']);voice=copy.deepcopy(FIXTURE['voice'])
    original.update(input_gain=input_gain,enabled=wake_enabled);voice['voice_enabled']=voice_enabled
    assert 'gain' not in original
    saved=bilingual_replay.capture_settings(original,voice)
    link=bilingual_replay.ReplayLink(runtime)
    restored=bilingual_replay.restore_settings(link,saved)
    assert restored['input_gain']==input_gain and restored['threshold']==original['threshold']
    assert restored['enabled']==wake_enabled
    commands=[text for text,_ in runtime.calls]
    assert commands.index('agent wake off')<commands.index('agent wake gain '+str(input_gain))
    assert ('agent voice on' in commands)==voice_enabled
    assert ('agent wake on' in commands)==(wake_enabled and not voice_enabled)


def test_missing_real_field_is_rejected_before_mutating_state():
    wake=copy.deepcopy(FIXTURE['wake']);wake['gain']=wake.pop('input_gain')
    with pytest.raises(KeyError):bilingual_replay.capture_settings(wake,FIXTURE['voice'])


def test_export_clip_uses_query_mode_and_exact_payload(tmp_path):
    runtime=Runtime();link=bilingual_replay.ReplayLink(runtime);path=tmp_path/'previous.wav'
    link.off();report=export_clip(link,path)
    with wave.open(str(path),'rb') as source:
        assert (source.getnchannels(),source.getsampwidth(),source.getframerate())==(1,2,16000)
        assert source.readframes(source.getnframes())==runtime.clip
    assert report['sha256']==hashlib.sha256(runtime.clip).hexdigest()
    assert report['samples']==len(runtime.clip)//2
    assert all(query for text,query in runtime.calls if text.startswith('agent audio clip read '))


def test_existing_replay_engine_consumes_adapter_and_current_fields(tmp_path,monkeypatch):
    runtime=Runtime();link=bilingual_replay.ReplayLink(runtime)
    clock=SimpleNamespace(monotonic=lambda:runtime.clock,time=lambda:runtime.clock,
        sleep=lambda seconds:setattr(runtime,'clock',runtime.clock+seconds))
    monkeypatch.setattr(acoustic_test,'time',clock)
    monkeypatch.setattr(bilingual_replay,'time',clock)
    rows=[];labels={}
    for i in range(14):
        path=tmp_path/f'source-{i}.wav'
        with wave.open(str(path),'wb') as target:
            target.setparams((1,2,16000,0,'NONE','not compressed'))
            target.writeframes(struct.pack('<1600h',*([1000,-1000]*800)))
        label=int(i<8);labels[path.name]=label
        rows.append(dict(clip_id=path.stem,path=str(path),split='validation',label=label,
            language='zh' if i<4 else 'yue',source_group='fixture',seconds=.1,wake_end_sample=1600,
            text='你好小言' if label else ('你好','小言','嗨乐鑫','你好小燕','请把灯打开','现在几点钟')[i-8]))
    def play(path,device,gain):
        start=runtime.clock;runtime.clock+=.1
        runtime.wake['wakes']+=labels[Path(path).name]
        return dict(started=start,source_started=start,finished=runtime.clock,
            device_index=device,device_name='fixture',sample_rate=16000,channels=1,samples=1600)
    class InlineExecutor:
        def __init__(self,**kwargs):pass
        def __enter__(self):return self
        def __exit__(self,*args):pass
        def submit(self,func,*args):
            value=func(*args)
            return SimpleNamespace(done=lambda:True,result=lambda **kwargs:value)
    monkeypatch.setitem(sys.modules,'wave_play',SimpleNamespace(play=play))
    monkeypatch.setattr(acoustic_test,'ThreadPoolExecutor',InlineExecutor)
    args=SimpleNamespace(out=tmp_path,split='validation',per_language=4,negatives=6,meter=True,
        source_rms=.14,gain=.35,device=0)
    report={};acoustic_test.replay(link,args,rows,report)
    assert len(report['trials'])==14
    assert report['languages']['zh']['triggers']==4 and report['languages']['yue']['triggers']==4
    assert report['negatives']=={'total':6,'triggers':0}
    assert not report['resources']['failures']
    assert all(row['observed_after_playback_ms']>=1200 for row in report['trials'])
