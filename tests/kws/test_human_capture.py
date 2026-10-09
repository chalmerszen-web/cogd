"""Field evidence survives interruption; simulated hardware is not acoustic proof."""
import json
from pathlib import Path
import sys
from types import SimpleNamespace

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools/kws'))
import capture_human_review as capture


@pytest.mark.parametrize('failure', [None, 'open', 'ready', 'confirm', 'close'])
def test_capture_lifecycle(tmp_path, monkeypatch, failure):
    commands = []
    closed = []
    clock = [0.0]
    answers = 0

    class Link:
        def __init__(self, port, log):
            if failure == 'open':
                raise OSError('port busy')
            self.poll = 0

        def off(self):
            return {'enabled': False}

        def command(self, command):
            commands.append(command)
            if command == 'agent light get':
                return dict(r=3, g=4, b=5)
            if command == 'agent audio capture 5000':
                self.poll = 0
            if command == 'agent audio status':
                self.poll += 1
                if failure == 'ready':
                    return dict(recording=False, capture_stage=0)
                return dict(recording=self.poll == 1, capture_stage=2,
                            clip_ready=self.poll > 1, clip_ms=5000, capture_error='ok')
            return {}

        def close(self):
            closed.append(True)
            if failure == 'close':
                raise OSError('close failed')

    def export_clip(link, path):
        # Fixture marker only; never represented as a physical capture.
        path.write_bytes(b'simulated audio')
        return {'samples': 80000, 'sha256': 'simulation'}

    def answer(prompt):
        nonlocal answers
        answers += 1
        if failure == 'confirm' and answers == 3:
            raise KeyboardInterrupt()
        return ''

    def advance(seconds):
        clock[0] += seconds

    monkeypatch.setattr(capture, 'ROOT', tmp_path)
    monkeypatch.setattr(capture, 'time', SimpleNamespace(
        sleep=advance, monotonic=lambda: clock[0], time=lambda: clock[0]))
    monkeypatch.setattr('builtins.input', answer)
    monkeypatch.setitem(sys.modules, 'serial_test', SimpleNamespace(Link=Link))
    monkeypatch.setitem(sys.modules, 'calibrate_acoustic', SimpleNamespace(export_clip=export_clip))
    monkeypatch.setattr(sys, 'argv', ['capture', '--language', 'both', '--distance-m', '5',
                                    '--speaker', 'p01', '--repeats', '1', '--delay', '3'])
    expected = {'open': OSError, 'ready': TimeoutError, 'confirm': KeyboardInterrupt}
    if failure in expected:
        with pytest.raises(expected[failure]):
            capture.main()
    else:
        capture.main()
    report = json.loads(next(tmp_path.rglob('report.json')).read_text(encoding='utf8'))
    assert report['source_group'] == 'human-p01'
    assert report['distance_m_user_reported'] == 5
    assert not report['distance_verified_by_software'] and not report['training_allowed']
    assert report['complete'] == (failure in (None, 'close'))
    if failure == 'open':
        assert commands == [] and closed == []
    else:
        assert closed == [True]
        assert commands[-3:] == ['agent audio stop', 'agent wake off', 'agent light set 3 4 5']
    if failure == 'confirm':
        assert len(report['trials']) == 1
        assert report['trials'][0]['confirmation_pending']
        assert not report['trials'][0]['speaker_confirmed']
    if failure in (None, 'close'):
        assert [(t['language'], t['intended_label']) for t in report['trials']] == [
            ('zh', 1), ('zh', 0), ('yue', 1), ('yue', 0)]
        assert all(t['speaker_confirmed'] and not t['confirmation_pending'] for t in report['trials'])
    assert bool(report['cleanup_errors']) == (failure == 'close')
