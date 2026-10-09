import sys
from pathlib import Path
import numpy as np
import pytest

sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'training/kws'))
from train import sampling_groups


def test_contrast_sampling_keeps_classes_disjoint_and_old_default():
    data=dict(label=np.array([1,1,0,0,0,0]),language=np.array(['zh','yue','zh','yue','zh','yue']),
              sampling_group=np.array(['positive','positive','hard_negative','hard_negative','natural_negative','paired_negative']),
              text=np.array(['你好，小言','你好，小言','你好小燕','你好小王','','time-cut-prefix']))
    groups,p=sampling_groups(data,paired=True)
    assert [g.tolist() for g in groups]==[[0],[1],[2,3],[4],[5]]
    assert p==[.25,.25,.20,.10,.20]
    groups,p=sampling_groups(data,paired=True,contrast=True)
    assert [g.tolist() for g in groups]==[[0],[1],[2],[3],[4],[5]]
    assert sum(p)==1 and p==[.25,.25,.10,.10,.10,.20]
    with pytest.raises(ValueError):sampling_groups(data,contrast=True)
    data['text'][2]='你好小王'
    with pytest.raises(ValueError):sampling_groups(data,paired=True,contrast=True)
