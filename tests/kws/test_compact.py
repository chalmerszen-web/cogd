import sys
from pathlib import Path
import numpy as np
import pytest

sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'training/kws'))
from train import focus_groups


def test_focus_preserves_class_membership_and_requires_existing_source():
    data=dict(clip_id=np.array(['old-zh','compact-zh','old-yue','compact-half','other']))
    groups=[np.array([0,1]),np.array([2]),np.array([3,4])]
    focused=focus_groups(data,groups,'compact-')
    assert [g.tolist() for g in focused]==[[1],[],[3]]
    assert [g.tolist() for g in groups]==[[0,1],[2],[3,4]]
    assert all(not len(g) for g in focus_groups(data,groups,None))
    with pytest.raises(ValueError):focus_groups(data,groups,'absent-')
