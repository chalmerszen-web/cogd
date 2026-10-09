"""Offline clock identity/omission checks. No hardware, audio or cloud."""
import copy
import json
import unittest

from inspect_tail import audit, capture_tail


def fixture():
    trials, sound = [], []
    for round_id in (1,2):
        start = round_id*1000
        wake = dict(detected_at=start-1,record_at=start,wakes=round_id)
        events = [dict(stage=s,time_ms=start+t,text='') for s,t in
            [('vad_end',200),('asr_finish',200),('asr_final_received',230),
             ('asr_text',233),('capture_radio_restored',235),('candidate_miss',238),
             ('fast_local_intent',239),('tool_done',240),('fast_local_reply',241),
             ('playback',243),('speaker_start',251)]]
        events.append(dict(stage='asr_upload_stats',text='{"samples":3200}'))
        attempt = dict(attempt=1,events=events,prompt_playback={'source_started':1},
            terminal='@done',last_state=dict(wake=wake))
        trials.append(dict(round=round_id,input_complete=True,attempts=[attempt]))
        sound.append(dict(round=round_id,attempt=1,source_match_accepted=True,status='candidate',
            input_active_end_recording_s=1,acoustic_answer_onset_candidate_s=2,
            acoustic_answer_latency_candidate_s=1,endpoint_cue_acoustic=dict(present=True,start_s=1.7,end_s=1.88)))
    final = dict(trials[-1]['attempts'][0]['last_state']['wake'],samples=3200,end_at=2190,cue_end_at=2234)
    return dict(trials=trials,wake_after=final),dict(trials=sound)


class TailAudit(unittest.TestCase):
    def test_preheat_trace_requires_zero_budget_and_full_commit_order(self):
        # The180ms zero lead may partly overlap verification. Reject a cue
        # enqueued too early, stale output clocks, wrong rate, or mixed traces.
        values=[1000,1180,1190,1191,1192,1350,1351,1550,1554,3200,0,1192,1352,320,16000]
        current=dict(record_at=1000)
        trace=dict(stage='capture_tail_v2',text=json.dumps(values))
        clock=capture_tail([trace],current,3200)
        self.assertEqual(clock['zero_samples'],320)
        for index,value in [(13,0),(11,1000),(12,1340),(14,48000),(13,32000)]:
            changed=list(values);changed[index]=value
            with self.assertRaises(ValueError):capture_tail([dict(stage='capture_tail_v2',text=json.dumps(changed))],current,3200)
        with self.assertRaises(ValueError):capture_tail([trace,dict(stage='capture_tail_v1',text=json.dumps(values[:11]))],current,3200)

    def test_failed_acoustic_fit_keeps_timing_unknown_but_board_clocks_available(self):
        r,a = fixture()
        a=dict(trials=[],status='unknown',reason='Fixed gates failed',source_alignment=dict(accepted=False))
        rows=audit(r,a)['rows']
        self.assertEqual(len(rows),2)
        self.assertTrue(all(t['sound_status']=='unknown' for t in rows))
        self.assertTrue(all(v is None for t in rows for v in t['acoustic_metrics_s'].values()))
        self.assertEqual(rows[-1]['intervals_ms']['capture_end_to_cleanup_ms'],44)
        a['source_alignment']['accepted']=True
        with self.assertRaises(ValueError):audit(r,a)

    def test_per_turn_trace_can_supply_earlier_capture_clocks(self):
        r,a = fixture()
        events = r['trials'][0]['attempts'][0]['events']
        values = [1000,1180,1190,1191,1192,1200,1201,1230,1234,3200,0]
        events.append(dict(stage='capture_tail_v1',text=json.dumps(values)))
        row = audit(r,a)['rows'][0]
        self.assertEqual(row['capture_snapshot']['samples'],3200)
        self.assertEqual(row['intervals_ms']['physical_stop_to_confirmation_ms'],10)
        self.assertEqual(row['intervals_ms']['commit_wall_ms'],8)
        self.assertEqual(row['intervals_ms']['cue_wall_ms'],29)
        self.assertEqual(row['intervals_ms']['capture_end_to_cleanup_ms'],44)

    def test_invalid_trace_is_not_hidden_by_last_snapshot_fallback(self):
        for data in ('not json','{}','[true]', '[1000,1180,1190,1191,1192,1200,1201,1230,1234,3200,0]'):
            r,a = fixture()
            r['trials'][-1]['attempts'][0]['events'].append(dict(stage='capture_tail_v1',text=data))
            row = audit(r,a)['rows'][-1]
            self.assertIsNone(row['capture_snapshot'])
            self.assertTrue(row['issues'])

    def test_trace_clocks_wrap_and_reject_partial_or_wrong_samples(self):
        base = 0xfffffff0
        values = [(base+x) & 0xffffffff for x in (0,1,2,3,4,5,6,17,18)]+[3200,0]
        current = dict(record_at=base)
        traces = [dict(stage='capture_tail_v1',text=json.dumps(values))]
        self.assertEqual(capture_tail(traces,current,3200)['cue_end_at'],1)
        with self.assertRaises(ValueError):capture_tail(traces,current,3000)
        for index,value in [(1,0),(5,base),(10,1),(0,True)]:
            changed=list(values);changed[index]=value
            with self.assertRaises(ValueError):capture_tail([dict(stage='capture_tail_v1',text=json.dumps(changed))],current,3200)
        with self.assertRaises(ValueError):capture_tail(traces+traces,current,3200)

    def test_earlier_turn_clocks_stay_missing(self):
        r,a = fixture();rows = audit(r,a)['rows']
        self.assertIsNone(rows[0]['capture_snapshot'])
        self.assertIsNone(rows[0]['intervals_ms']['capture_end_to_upload_eof_ms'])
        self.assertEqual(rows[1]['intervals_ms']['capture_end_to_upload_eof_ms'],10)
        self.assertEqual(rows[1]['intervals_ms']['capture_end_to_cleanup_ms'],44)
        self.assertEqual(rows[1]['intervals_ms']['upload_eof_to_cleanup_ms'],34)
        self.assertEqual(rows[0]['intervals_ms']['final_event_to_asr_text_ms'],3)

    def test_mismatched_last_snapshot_never_becomes_a_capture_clock(self):
        for key in ('detected_at','record_at','wakes','samples','end_at','cue_end_at'):
            with self.subTest(key=key):
                r,a = fixture();r['wake_after'][key] = 0
                row = audit(r,a)['rows'][-1]
                self.assertIsNone(row['capture_snapshot'])
                self.assertIsNone(row['intervals_ms']['capture_end_to_cleanup_ms'])

    def test_duplicates_and_reversed_clocks_stay_missing(self):
        r,a = fixture();events = r['trials'][0]['attempts'][0]['events']
        events.append(copy.deepcopy(events[0]))
        next(e for e in events if e['stage'] == 'playback')['time_ms'] = 1001
        row = audit(r,a)['rows'][0]
        self.assertIsNone(row['intervals_ms']['upload_eof_to_final_asr_ms'])
        self.assertIsNone(row['intervals_ms']['final_asr_to_first_pcm_ms'])
        self.assertTrue(row['issues'])

    def test_unknown_and_unmatched_source_never_get_acoustic_latency(self):
        for key,value in [('source_match_accepted',False),('status','unknown')]:
            r,a = fixture();a['trials'][0][key] = value
            row = audit(r,a)['rows'][0]
            self.assertIsNone(row['acoustic_metrics_s']['source_end_to_answer_candidate_s'])
        r,a = fixture();a['trials'][0]['endpoint_cue_acoustic']['present'] = False
        self.assertTrue(all(v is None for v in audit(r,a)['rows'][0]['acoustic_metrics_s'].values()))

    def test_sources_must_have_same_order_and_attempt(self):
        for changed in ('count','round','attempt','multiple_prompts'):
            with self.subTest(changed=changed):
                r,a = fixture()
                if changed == 'count':a['trials'].pop()
                elif changed == 'round':a['trials'][0]['round'] = 3
                elif changed == 'attempt':a['trials'][0]['attempt'] = 2
                else:r['trials'][0]['attempts'].append(copy.deepcopy(r['trials'][0]['attempts'][0]))
                with self.assertRaises(ValueError):audit(r,a)


if __name__ == '__main__':
    unittest.main()
