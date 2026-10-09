"""Bounded augmentation contracts; never load audio, DEV, or model weights."""
from pathlib import Path
import hashlib
import json
import sys
import time
import numpy as np
import torch

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'training/kws'))
from frequency_mask import frequency_mask


def verify():
    began=time.monotonic()
    torch.set_num_threads(1)
    rng=np.random.default_rng(20261002323)
    checked=0
    for batch,frames in ((1,1),(32,256),(33,256),(7,513)):
        source=torch.from_numpy(rng.uniform(.25,4,(batch,40,frames)).astype(np.float32)).requires_grad_(True)
        before=source.detach().clone()
        out,starts,widths=frequency_mask(source,np.random.default_rng(11),probability=1)
        expected=before.clone()
        expected_gradient=torch.ones_like(source)
        for i,(start,width) in enumerate(zip(starts,widths)):
            assert 0<=width<=4 and 0<=start<=40-width
            if width:
                expected[i,start:start+width]=0
                expected_gradient[i,start:start+width]=0
        torch.testing.assert_close(out,expected,rtol=0,atol=0)
        torch.testing.assert_close(source.detach(),before,rtol=0,atol=0)
        assert out.shape==source.shape and out.dtype==source.dtype and out.data_ptr()!=source.data_ptr()
        out.sum().backward()
        torch.testing.assert_close(source.grad,expected_gradient,rtol=0,atol=0)
        checked+=source.numel()
    source=torch.arange(32*40*256,dtype=torch.float32).reshape(32,40,256)
    a,b=np.random.default_rng(73),np.random.default_rng(73)
    schedules=[]
    for _ in range(16):
        oa,sa,wa=frequency_mask(source,a)
        ob,sb,wb=frequency_mask(source,b)
        torch.testing.assert_close(oa,ob,rtol=0,atol=0)
        np.testing.assert_array_equal(sa,sb);np.testing.assert_array_equal(wa,wb)
        schedules.extend(wa.tolist())
    assert any(schedules) and any(w==0 for w in schedules)
    for options in ({'probability':0},{'maximum':0}):
        out,_,widths=frequency_mask(source,np.random.default_rng(7),**options)
        torch.testing.assert_close(out,source,rtol=0,atol=0)
        assert not widths.any()
    rejected=0
    for options in ({'maximum':5},{'maximum':-1},{'maximum':True},
                    {'probability':-1},{'probability':1.1},{'probability':float('nan')}):
        try:frequency_mask(source,np.random.default_rng(7),**options)
        except ValueError:rejected+=1
        else:raise AssertionError('Invalid augmentation option accepted')
    for bad in (torch.zeros(1,39,256),torch.zeros(0,40,256),torch.zeros(1,40,0),
                torch.zeros(1,40,256,dtype=torch.int8)):
        try:frequency_mask(bad,np.random.default_rng(7))
        except ValueError:rejected+=1
        else:raise AssertionError('Invalid features accepted')
    return dict(complete=True,checked_values=checked,seeded_batches=16,
        invalid_cases_rejected=rejected,source_unmodified=True,
        shape_and_time_axis_preserved=True,outside_mask_exact=True,
        autograd_exact=True,zero_and_probability_disabled_exact=True,
        maximum_width=4,seconds=time.monotonic()-began,
        new_training=False,DEV_or_TEST_read=False,hardware=False,
        source_sha256=hashlib.sha256((ROOT/'training/kws/frequency_mask.py').read_bytes()).hexdigest())


if __name__=='__main__':
    assert len(sys.argv)==2
    out=Path(sys.argv[1])
    assert not out.exists()
    out.mkdir(parents=True)
    result=verify()
    (out/'checks.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf8')
    (out/'process.json').write_text(json.dumps(dict(terminal=True,exit_code=0,seconds=result['seconds']))+'\n',encoding='utf8')
    print(json.dumps(result))
