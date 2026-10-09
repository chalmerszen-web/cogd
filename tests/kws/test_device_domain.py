from pathlib import Path
import sys
import numpy as np
import pytest
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'training/kws'))
from device_domain import align,align_waveform
from evaluate import metrics
from calibrate import calibration_indices


def test_envelope_alignment_recovers_physical_delay_with_noise():
    rng=np.random.default_rng(21);t=np.arange(24000)/16000
    envelope=(np.sin(2*np.pi*2.7*t)**2+.1)*np.hanning(len(t))
    source=(4000*envelope*np.sin(2*np.pi*370*t)).astype(np.int16)
    recorded=rng.normal(0,70,96000)
    recorded[9120:9120+len(source)]+=.45*source
    lag,correlation=align(source,recorded)
    assert abs(lag-9120)<=160 and correlation>.95
    with pytest.raises(ValueError):align(np.zeros(16000),recorded)


def test_device_failures_are_visible_separately_from_clean_successes():
    scores=np.zeros((4,256));scores[:2,130:134]=100
    data=dict(label=np.ones(4),language=np.array(['zh','yue','zh','yue']),
              event_end=np.full(4,33280),device_domain=np.array([False,False,True,True]))
    result=metrics(scores,data,50)
    assert result['languages']['zh']['recall']==.5
    assert result['device']['languages']['zh']['recall']==0
    assert result['device']['languages']['yue']['recall']==0


def test_calibration_includes_both_domains_without_duplicates():
    domain=np.r_[np.zeros(1000,dtype=bool),np.ones(300,dtype=bool)]
    indices=calibration_indices(np.random.default_rng(1),len(domain),512,domain)
    assert len(indices)==len(set(indices))==512 and domain[indices].sum()==256
    indices=calibration_indices(np.random.default_rng(1),len(domain),2000,domain)
    assert len(indices)==len(set(indices))==1300


def test_waveform_delay_under_filtered_weak_speech_and_wrong_source_rejection():
    rng=np.random.default_rng(97)
    source=np.convolve(rng.normal(0,800,20000),np.array([1.,.5,.2]),mode='same')
    recorded=rng.normal(0,130,64000)
    lag=8927;response=np.convolve(source,[.15,.06,-.03],mode='full')
    recorded[lag:lag+len(response)]+=response
    measured,quality=align_waveform(source,recorded,8320)
    assert abs(measured-lag)<=2 and quality['peak_rms_ratio']>=7.5
    for _ in range(12):
        unrelated=rng.normal(0,800,20000)
        with pytest.raises(ValueError):align_waveform(unrelated,recorded,8320)
    with pytest.raises(ValueError):align_waveform(np.zeros(20000),recorded,8320)
