"""Regression for a late periodic status response preceding a different USB query."""
import io
import unittest
from device import Device


class Link:
    def __init__(self,data):self.data=data
    def write(self,data):return len(data)
    def read(self,count):
        data=self.data[:count];self.data=self.data[count:];return data


def device(data):
    d=Device.__new__(Device);d.link=Link(data);d.buffer=b'';d.rows=[];d.unread=[];d.raw=io.BytesIO()
    return d


class Replies(unittest.TestCase):
    def test_tool_query_consumes_its_ack_before_next_command(self):
        d=device(b'{"job_id":2,"rendered":true}\n@ok ok\n'
                 b'{"job_id":3,"rendered":false}\n@ok ok\n')
        self.assertEqual(d.command('agent tool qianwen_skill {"action":"status"}',query=True)['job_id'],2)
        row=d.command('agent tool device_camera_test {}',timeout=.1)
        self.assertIn('{"job_id":3,"rendered":false}',row['lines'])

    def test_late_status_does_not_shift_following_queries(self):
        d=device(b'{"firmware":"esp-hi-agent"}\n{"audio_ready":true}\n{"state":"listening"}\n')
        self.assertEqual(d.command('agent audio status',query=True,timeout=.1),{'audio_ready':True})
        self.assertEqual(d.command('agent wake status',query=True,timeout=.1),{'state':'listening'})

    def test_wrong_type_is_not_success(self):
        d=device(b'{"firmware":"esp-hi-agent"}\n')
        with self.assertRaises(TimeoutError):d.command('agent voice status',query=True,timeout=.01)

    def test_streamed_answer_prefix_does_not_hide_status_json(self):
        d=device('下次再聊。{"firmware":"esp-hi-agent","busy":false}\n'.encode())
        self.assertFalse(d.command('agent status',query=True,timeout=.1)['busy'])
        self.assertTrue(d.rows[0]['lines'][0].startswith('下次再聊。'))
        d=device('下次再聊。{"audio_ready":true}\n'.encode())
        with self.assertRaises(TimeoutError):d.command('agent status',query=True,timeout=.01)

    def test_split_event_before_query_does_not_replace_deadline(self):
        prefix=b'@voice {"stage":"asr_partial","time_ms":42}\n'
        d=device(prefix+b'{"state":"recording"}\n')
        original=d.link.read
        d.link.read=lambda count:original(min(count,len(prefix)))
        self.assertEqual(d.command('agent wake status',query=True,timeout=.1)['state'],'recording')

    def test_error_is_not_ignored(self):
        d=device(b'@error forbidden\n')
        with self.assertRaises(RuntimeError):d.command('agent light get',query=True,timeout=.1)

    def test_cancelled_job_can_finish_before_cancel_ack(self):
        d=device(b'@error cancelled\n@ok ok\n')
        self.assertTrue(d.command('agent cancel',timeout=.1)['cancelled_job'])

    def test_cancel_ack_can_precede_job_completion(self):
        d=device(b'@ok ok\n@error cancelled\n')
        d.command('agent cancel',timeout=.1)
        self.assertIn('@error cancelled',d.lines())

    def test_expected_barge_cancel_does_not_resubmit_query(self):
        d=device(b'@error cancelled\n{"duplex":false,"followup_ms":9000}\n')
        self.assertEqual(d.command('agent voice dialogue',query=True,timeout=.1,cancelled_ok=True)['followup_ms'],9000)
        self.assertEqual(len(d.rows),1)
        self.assertTrue(d.rows[0]['cancelled_job'])
        d=device(b'@error cancelled\n')
        with self.assertRaises(RuntimeError):d.command('agent voice dialogue',query=True,timeout=.1)

    def test_voice_events_inside_queries_are_retained_once(self):
        d=device(b'@voice {"stage":"asr_audio","time_ms":42}\n{"state":"recording"}\n'
                 b'@voice {"stage":"asr_partial","time_ms":43,"text":"hello"}\n')
        self.assertEqual(d.command('agent wake status',query=True)['state'],'recording')
        d.lines()
        self.assertEqual([e['stage'] for e in d.events],['asr_audio','asr_partial'])

    def test_profile_and_clip_queries_ignore_late_status(self):
        d=device(b'{"firmware":"esp-hi-agent"}\n{"trained":true,"blocks":5}\n'
                 b'{"audio_ready":true}\n{"offset":256,"pcm":"0000"}\n')
        self.assertEqual(d.command('agent kws profile',query=True,timeout=.1),{'trained':True,'blocks':5})
        self.assertEqual(d.command('agent audio clip read 256 1',query=True,timeout=.1),{'offset':256,'pcm':'0000'})


if __name__=='__main__':unittest.main()
