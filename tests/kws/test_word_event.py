from pathlib import Path
import itertools,sys,unittest
import numpy as np
import torch
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'training/kws'))
from word_event import WordEventModel,event_loss,event_targets,event_logits,single_event_probability


def exact_nll(logits,label,window,prefix):
    raw=logits.detach().numpy()[:,1::2].T
    p=np.exp(raw-raw.max(1,keepdims=True));p/=p.sum(1,keepdims=True)
    fixed=float(p[:prefix//2,0].prod());p=p[prefix//2:]
    if not label:return -np.log(fixed*p[:,0].prod())
    mass=0.
    for start in range(window[0],window[1]+1):
        for end in range(start,window[1]+1):
            mass+=p[:start,0].prod()*p[start:end+1,1].prod()*p[end+1:,0].prod()
    return -np.log(fixed*mass)


class WordEventTest(unittest.TestCase):
    def test_exact_paths_and_every_gradient(self):
        torch.manual_seed(462)
        logits=torch.randn(2,2,18,dtype=torch.float64,requires_grad=True)
        targets=torch.tensor([[1],[0]]);lengths=torch.tensor([1,0])
        windows=torch.tensor([[3,6],[-1,-1]])
        loss=event_loss(logits,targets,lengths,windows,2)
        def expected(value):return (exact_nll(value[0],1,(3,6),2)+exact_nll(value[1],0,(-1,-1),2))/2
        self.assertAlmostEqual(float(loss.detach()),expected(logits),places=12)
        loss.backward();self.assertTrue(torch.isfinite(logits.grad).all())
        for batch,channel,frame in itertools.product(range(2),range(2),range(18)):
            changed=logits.detach().clone();changed[batch,channel,frame]+=1e-5
            up=expected(changed);changed[batch,channel,frame]-=2e-5;down=expected(changed)
            self.assertAlmostEqual(float(logits.grad[batch,channel,frame]),(up-down)/2e-5,places=8)

    def test_event_window_independent_enumeration(self):
        rng=np.random.default_rng(462)
        for frames in range(1,9):
            raw=rng.normal(size=(frames,2));p=np.exp(raw-raw.max(1,keepdims=True));p/=p.sum(1,keepdims=True)
            expected=0.
            for path in itertools.product(range(2),repeat=frames):
                events=sum(token==1 and (i==0 or path[i-1]==0) for i,token in enumerate(path))
                if events==1:expected+=p[np.arange(frames),path].prod()
            self.assertAlmostEqual(single_event_probability(raw),expected,places=14)

    def test_model_stream_labels_and_bounds(self):
        torch.set_num_threads(1);torch.manual_seed(462)
        model=WordEventModel().eval()
        self.assertEqual(sum(p.numel() for p in model.parameters()),19154)
        x=torch.randn(2,40,160)
        with torch.no_grad():
            expected=model(x);state=None;parts=[]
            for frame in x.split(1,dim=-1):
                value,state=model.step(frame,state);parts.append(value)
            actual=torch.cat(parts,-1)
        torch.testing.assert_close(actual,expected,rtol=1e-5,atol=1e-6)
        target,size=event_targets(np.array([1,0,1],np.int64))
        np.testing.assert_array_equal(target,[[1],[0],[1]])
        np.testing.assert_array_equal(size,[1,0,1])
        raw=np.array([[.5/256,-.5/256],[200,-200]],np.float64).T.copy().T
        converted=event_logits(raw)
        self.assertTrue(converted.flags.c_contiguous)
        np.testing.assert_array_equal(converted,[[1,-1],[32767,-32768]])
        with self.assertRaises(ValueError):event_logits(np.zeros((2,14)))
        logits=torch.zeros(1,2,12);targets=torch.tensor([[1]]);lengths=torch.tensor([1])
        for window in ((-1,-1),(2,1),(0,6)):
            with self.assertRaises(ValueError):event_loss(logits,targets,lengths,torch.tensor([window]),0)
        with self.assertRaises(ValueError):event_targets(np.array([2],np.int64))


if __name__=='__main__':unittest.main()
