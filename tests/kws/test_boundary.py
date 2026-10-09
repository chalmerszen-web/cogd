from pathlib import Path
import sys
import numpy as np
import pytest
import torch
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'training/kws'))
from boundary import interval,place,sustained
from train import masked_bce

def test_end_excludes_padding_and_short_click():
    pcm=np.zeros(16000);pcm[1600:9600]=4000*np.sin(np.arange(8000)*.2)
    pcm[14400:14560]=5000
    r=interval(pcm)
    assert r['speech_end_low_sample']==9600
    assert r['speech_end_high_sample']==9760

def test_sustained_does_not_join_isolated_peaks():
    np.testing.assert_array_equal(sustained([1,1,0,1,1,1,0]),[0,0,0,1,1,1,0])

def test_same_position_for_positive_and_partial_negative():
    pcm=np.zeros(24000);pcm[1600:16000]=2000
    row=dict(speech_end_low_sample=15840,speech_end_high_sample=16320,label=1)
    positive,labels,end=place(row,pcm)
    negative,zeros,none=place({**row,'label':0},pcm)
    np.testing.assert_array_equal(positive,negative)
    assert (labels==-1).any() and (labels==1).any() and not zeros.any() and none is None
    assert end>32768 and np.isin(labels,[-1,0,1]).all()

def test_uncertain_frame_has_no_gradient():
    x=torch.tensor([[[1.,2.,3.]]],requires_grad=True);y=torch.tensor([[[0.,-1.,1.]]])
    loss=masked_bce(x,y);loss.backward()
    assert x.grad[0,0,1]==0 and x.grad[0,0,0]>0 and x.grad[0,0,2]<0

def test_uncertainty_over_limit_is_rejected():
    pcm=np.zeros(16000);pcm[1600:8000]=3000;pcm[8000:11000]=50
    with pytest.raises(ValueError,match='uncertain_end'):interval(pcm)

def test_interval_evaluation_and_partial_word_reporting():
    from evaluate import metrics
    scores=np.full((3,256),-1000,dtype=np.int16)
    scores[:,127]=500;scores[:,129]=500
    data=dict(label=np.array([1,1,0]),language=np.array(['zh','yue','yue']),
        event_end=np.array([33792,33792,-1]),event_start_accept=np.array([33280,33280,-1]),
        sampling_group=np.array(['positive','positive','hard_negative']),text=np.array(['目标','目标','小言']))
    result=metrics(scores,data,425)
    assert result['languages']['yue']['hits']==1 and result['premature_events']==0
    assert result['partial_word_total']==1 and result['partial_word_triggers']==1
    assert result['negative_groups']['hard_negative']==dict(total=1,triggered=1)
