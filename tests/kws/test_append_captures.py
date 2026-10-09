from pathlib import Path
import sys
import numpy as np
import pytest
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'training/kws'))
from append_captures import respeed,capture_path


def test_speed_changes_audio_and_uncertainty_interval_together():
    row=dict(speech_start_sample=1000,speech_end_low_sample=15000,speech_end_high_sample=15600)
    signal=np.zeros(17000,dtype=np.int16);signal[1000:15000]=500
    result,moved=respeed(signal,row,.85)
    assert len(result)==round(17000/.85)
    assert moved['speech_start_sample']==round(1000/.85)
    assert moved['speech_end_high_sample']-moved['speech_end_low_sample']==round(15600/.85)-round(15000/.85)
    assert result[round(8000/.85)]==500
    assert row['speech_end_low_sample']==15000
    result,unchanged=respeed(signal,row,1.)
    np.testing.assert_array_equal(signal,result);assert row==unchanged


def test_speed_never_truncates_a_word_to_fit():
    row=dict(speech_start_sample=0,speech_end_low_sample=30000,speech_end_high_sample=31000)
    with pytest.raises(ValueError):respeed(np.zeros(32000),row,.85)
    with pytest.raises(ValueError):respeed(np.zeros(32000),row,1.5)


def test_windows_capture_paths_map_to_same_workspace_under_wsl(monkeypatch):
    import append_captures
    root=Path('/mnt/c/Users/PC/Documents/ChatGPT/cogd')
    monkeypatch.setattr(append_captures,'ROOT',root)
    assert capture_path('C:/Users/PC/Documents/ChatGPT/cogd/artifacts/a.wav')==root/'artifacts/a.wav'
    assert capture_path('artifacts/a.wav')==root/'artifacts/a.wav'
