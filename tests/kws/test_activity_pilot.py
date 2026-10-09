from pathlib import Path
import sys
import numpy as np
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'tools/kws'))
from activity_gate_pilot import recent_energy,gated_events


def test_energy_expiry_rejects_delayed_peak_and_accepts_new_speech():
    signal=np.full(65536,100,dtype=np.int16);signal[70*512:74*512]=500
    allowed,floor=recent_energy(signal)
    assert floor==10000 and allowed[78] and not allowed[79]
    scores=np.full(256,-1000);scores[142:150]=1000;scores[170:178]=1000
    assert gated_events(scores,500,allowed)==[73*512]


def test_rejected_peak_does_not_consume_cooldown_before_real_speech():
    allowed=np.zeros(128,dtype=bool);allowed[79:85]=True
    scores=np.full(256,-1000);scores[138:148]=1000;scores[160:170]=1000
    assert gated_events(scores,500,allowed)==[82*512]
