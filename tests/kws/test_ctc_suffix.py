from pathlib import Path
from collections import defaultdict
import itertools,sys,unittest
import numpy as np
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'training/kws'))
from ctc_suffix import SuffixBeam


class SuffixTest(unittest.TestCase):
    def test_independent_all_paths_and_ages(self):
        rng=np.random.default_rng(459)
        raw=rng.normal(size=(5,14));raw[:,6:]=-np.inf
        p=np.exp(raw[:,:6]-raw[:,:6].max(1,keepdims=True));p/=p.sum(1,keepdims=True)
        # No pruning here: independently enumerate ALL7776 paths, merge only
        # their collapsed last4 labels and final blank/nonblank state.
        probability=defaultdict(lambda:np.zeros(2))
        best={};ages={}
        for path in itertools.product(range(6),repeat=5):
            labels=[];times=[];last=None
            for t,label in enumerate(path):
                if label and label!=last:labels.append(label);times.append(t)
                last=label
            key=tuple(labels[-4:]);slot=0 if path[-1]==0 else 1
            mass=float(np.prod(p[np.arange(5),path]))
            probability[key][slot]+=mass
            age=tuple(4-t for t in times[-4:])
            state=(key,slot)
            if mass>best.get(state,-1) or (mass==best.get(state) and age<ages[state]):
                best[state]=mass;ages[state]=age
        beam=SuffixBeam(width=4096)
        for row in raw:beam.step(row)
        self.assertEqual(set(probability),set(beam.paths))
        for key,expected in probability.items():
            row=beam.paths[key]
            np.testing.assert_allclose(np.exp(np.array(row[:2])+beam.log_offset),expected,
                                       rtol=1e-12,atol=1e-15)
            for slot in (0,1):
                if expected[slot]:
                    self.assertAlmostEqual(float(np.exp(row[slot+2]+beam.viterbi_offset)),best[key,slot],places=14)
                    self.assertEqual(row[slot+4],ages[key,slot])

    def test_continuous_state_bounds_and_old_context(self):
        beam=SuffixBeam()
        def row(token):
            x=np.full(14,-100.);x[token]=0;return x
        sequence=[]
        for token in (1,2,3,4):
            sequence.extend((row(token),row(0)))
        for x in sequence:prefix,age=beam.step(x)
        self.assertEqual(prefix,(1,2,3,4))
        self.assertLess(age[0]*512,48000)
        for _ in range(2000):prefix,age=beam.step(row(0))
        self.assertEqual(prefix,(1,2,3,4))
        self.assertEqual(age,(255,255,255,255))
        for token in (5,6,7,8):
            beam.step(row(token));prefix,age=beam.step(row(0))
        self.assertEqual(prefix,(5,6,7,8))
        self.assertLess(age[0]*512,48000)
        self.assertLessEqual(len(beam.paths),8)
        self.assertTrue(all(len(key)<=4 for key in beam.paths))
        beam.reset();self.assertEqual(beam.ranked(),[((),0.)])
        for bad in (np.zeros(13),np.full(14,np.nan),np.full(14,np.inf),np.full(14,-np.inf)):
            with self.assertRaises(ValueError):beam.step(bad)


if __name__=='__main__':unittest.main()
