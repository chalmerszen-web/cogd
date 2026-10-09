from pathlib import Path
import sys
import numpy as np
import pytest
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'training/kws'))
from paired_negative import incomplete


def test_half_phrase_removes_complement_and_fades_cut_without_wraparound():
    signal=np.full(16000,1000,dtype=np.int16)
    prefix,cut=incomplete(signal,1600,14400,'prefix')
    suffix,other=incomplete(signal,1600,14400,'suffix')
    assert cut==other==8000
    assert np.all(prefix[cut:]==0) and np.all(suffix[:cut]==0)
    assert prefix[cut-1]==0 and suffix[cut]==0
    assert np.all(prefix[:cut-320]==1000) and np.all(suffix[cut+320:]==1000)
    assert np.all(np.diff(prefix[cut-320:cut].astype(int))<=0)
    assert np.all(np.diff(suffix[cut:cut+320].astype(int))>=0)


def test_capture_remainder_uses_ambient_and_invalid_cuts_fail():
    signal=np.full(16000,2000,dtype=np.int16);ambient=np.full(1000,30,dtype=np.int16)
    part,cut=incomplete(signal,500,15000,'suffix',.6,ambient)
    assert np.all(part[:cut]==30) and np.all(part[cut+320:]==2000)
    for fraction in (0,.2,.9,1):
        with pytest.raises(ValueError):incomplete(signal,500,15000,'suffix',fraction)
