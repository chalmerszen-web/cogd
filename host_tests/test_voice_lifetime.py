import copy
import json
from pathlib import Path
import sys
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools/voice'))
from analyze_voice_lifetime import ring

class Tests(unittest.TestCase):
    def events(self):
        return [dict(stage='voice_heap_point',text=json.dumps(dict(seq=i+1,at_ms=i,free=100-i,phase='rearm_begin')))
                for i in range(3)]+[dict(stage='voice_heap_points_summary',text=json.dumps(dict(points=3,overwritten=0,skipped=0)))]
    def decode(self,events):return ring(events,'voice_heap_point','voice_heap_points_summary','points')
    def test_complete_and_missing_records(self):
        events=self.events();self.assertTrue(self.decode(events)['available'])
        for bad in (events[1:],events+[events[-1]],events[:2]+events[1:],events[1:2]+events[:1]+events[2:]):
            self.assertFalse(self.decode(bad)['available'])
    def test_skipped_callbacks_are_not_called_complete(self):
        events=self.events();events[-1]['text']=json.dumps(dict(points=3,overwritten=0,skipped=2))
        result=self.decode(events);self.assertTrue(result['available']);self.assertFalse(result['all_callbacks_observed'])
    def test_invalid_clock_phase_and_counter(self):
        for patch in ({'at_ms':-1},{'phase':'invented'},{'at_ms':False}):
            events=self.events();row=json.loads(events[0]['text']);row.update(patch);events[0]['text']=json.dumps(row)
            with self.assertRaises(ValueError):self.decode(events)
        events=self.events();events[-1]['text']=json.dumps(dict(points=-1,overwritten=0,skipped=0))
        with self.assertRaises(ValueError):self.decode(events)
    def test_overwrite_is_valid_only_for_exact_tail(self):
        events=[dict(stage='voice_heap_point',text=json.dumps(dict(seq=i,at_ms=i,free=1,phase='cleanup_end'))) for i in range(7,23)]
        events.append(dict(stage='voice_heap_points_summary',text=json.dumps(dict(points=22,overwritten=6,skipped=0))))
        self.assertTrue(self.decode(events)['available']);self.assertFalse(self.decode(events[1:])['available'])
    def test_contradictory_minima_rejected(self):
        events=[dict(stage='voice_heap',text=json.dumps(dict(seq=i+1,at_ms=i,free=100+i))) for i in range(2)]
        events.append(dict(stage='voice_heap_summary',text=json.dumps(dict(minima=2,overwritten=0,skipped=0,minimum=101))))
        with self.assertRaises(ValueError):ring(events,'voice_heap','voice_heap_summary','minima')
    def test_sdk_drop_does_not_claim_simultaneous_free_drop(self):
        rows=[dict(seq=1,at_ms=1,free=50000,sdk_minimum=45000),
              dict(seq=2,at_ms=2,free=98000,sdk_minimum=46464-3000)]
        summary=dict(minima=2,overwritten=0,skipped=0,minimum=50000,sdk_minimum=43464,
                     initial=100000,initial_sdk_minimum=90000)
        events=[dict(stage='voice_heap',text=json.dumps(r)) for r in rows]
        events.append(dict(stage='voice_heap_summary',text=json.dumps(summary)))
        result=ring(events,'voice_heap','voice_heap_summary','minima')
        self.assertTrue(result['available']);self.assertEqual(result['rows'][-1]['free'],98000)
        for bad in (45000,46000):
            changed=copy.deepcopy(events);row=json.loads(changed[1]['text']);row['sdk_minimum']=bad
            changed[1]['text']=json.dumps(row)
            with self.assertRaises(ValueError):ring(changed,'voice_heap','voice_heap_summary','minima')

if __name__=='__main__': unittest.main()
