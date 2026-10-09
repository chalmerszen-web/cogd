"""Audit a completed host-only reuse observation without credentials or network."""
import argparse
import base64
from collections import Counter
import hashlib
import json
from pathlib import Path
import wave


def sha(data):
    return hashlib.sha256(data).hexdigest()


def audit(path):
    report = json.loads((path/'report.json').read_text(encoding='utf8'))
    assert report['attempts'] == 1 and report['retries'] == 0
    assert not report['played'] and not report['tools_executed'] and not report['accepted']
    assert sha((path/'executed-script.py').read_bytes()) == report['script_sha256']
    traffic = [json.loads(line) for line in (path/'traffic.jsonl').read_text(encoding='utf8').splitlines()]
    sends = [row for row in traffic if row['direction'] == 'send']
    events = [row for row in traffic if row['direction'] == 'receive']
    wire = [json.loads(line) for line in (path/'wire.jsonl').read_text(encoding='utf8').splitlines()]
    assert [row['event'] for row in events] == wire
    assert len({row['event_id'] for row in sends}) == len(sends)
    assert len({event['event_id'] for event in wire}) == len(wire)
    counts = Counter(event['type'] for event in wire)
    sent_counts = Counter(row['type'] for row in sends)
    confirmations = sum('draft_response' in turn for turn in report['turns'])
    response_count = 3+confirmations
    assert response_count <= report.get('max_model_responses', 3)
    assert sent_counts['session.update'] == 1
    assert sent_counts['response.create'] == counts['response.done'] == response_count
    assert sent_counts['input_audio_buffer.commit'] == counts['input_audio_buffer.committed'] == 3
    assert len(report['turns']) == 3
    rows = []
    for index, turn in enumerate(report['turns']):
        end = report['turns'][index+1]['start_s'] if index < 2 else report['finished_s']
        sent = [row for row in sends if turn['start_s'] <= row['observed_s'] < end]
        sizes = [row['samples'] for row in sent if row['type'] == 'input_audio_buffer.append']
        assert sizes and all(0 < count <= report['chunk_samples'] for count in sizes)
        assert sum(sizes) == turn['sent_samples'] == turn['expected_samples']
        assert sum(row['type'] == 'input_audio_buffer.commit' for row in sent) == 1
        assert sum(row['type'] == 'response.create' for row in sent) == 1+('draft_response' in turn)
        stages = ([turn['draft_response']] if 'draft_response' in turn else [])+[turn]
        for stage in stages:
            response_id = stage['response_id']
            response = [row for row in events if row['event'].get('response_id') == response_id
                        or row['event'].get('response', {}).get('id') == response_id]
            pcm = b''.join(base64.b64decode(row['event']['delta'], validate=True) for row in response
                           if row['event']['type'] == 'response.audio.delta')
            assert len(pcm) == stage['pcm_bytes'] and sha(pcm) == stage['pcm_sha256']
            with wave.open(str(path/stage['audio_file']), 'rb') as wav:
                assert (wav.getnchannels(), wav.getsampwidth(), wav.getframerate()) == (1, 2, report['output_rate'])
                assert wav.readframes(wav.getnframes()) == pcm
        input_id = turn['input_item_id']
        inputs = [row for row in events if row['event'].get('item_id') == input_id]
        final = [row for row in inputs if row['event']['type'] == 'conversation.item.input_audio_transcription.completed']
        acks = [row for row in inputs if row['event']['type'] == 'input_audio_buffer.committed']
        assert len(acks) == 1 and acks[0]['observed_s'] == turn['commit_ack_s']
        preview = [row for row in inputs if row['event']['type'] == 'conversation.item.input_audio_transcription.delta']
        assert bool(final) == bool(turn['final_asr'])
        if not final:
            assert turn['missing_final_after_8s'] and not turn['protocol_complete']
            observed_end = turn.get('clear_sent_s', report['finished_s'])
            assert observed_end-turn['commit_s'] >= 8
        last_preview = preview[-1]['event'] if preview else {}
        first = turn.get('draft_response', turn)
        rows.append(dict(turn=index+1, samples=sum(sizes), appends=len(sizes), tail_samples=sizes[-1],
            commit_ack_ms=round((turn['commit_ack_s']-turn['commit_s'])*1000, 3),
            draft_to_first_pcm_ms=round((first['first_pcm_s']-turn['draft_request_s'])*1000, 3),
            first_pcm_before_commit_ms=round((turn['commit_s']-first['first_pcm_s'])*1000, 3),
            response_done_after_commit_ms=round((turn['done_s']-turn['commit_s'])*1000, 3),
            final_asr_after_commit_ms=round((turn['final_asr_s']-turn['commit_s'])*1000, 3) if final else None,
            confirmation_generation='draft_response' in turn,
            confirmation_after_commit_ms=round((turn['confirmation_request_s']-turn['commit_s'])*1000, 3)
                if 'draft_response' in turn else None,
            final_asr_events=len(final), preview_events=len(preview),
            last_preview=last_preview.get('text', '')+last_preview.get('stash', ''),
            response_text=turn['text'], pcm_samples=len(pcm)//2, protocol_complete=turn['protocol_complete']))
    return dict(scope='host_protocol_observation_only', accepted=False,
                confirmation_generations=confirmations,
                wire_events=len(wire), sends=len(sends), event_counts=dict(counts),
                sent_counts=dict(sent_counts), turns=rows,
                input_model=wire[1]['session']['input_audio_transcription']['model'],
                hashes={name: sha((path/name).read_bytes()) for name in
                        ('report.json', 'executed-script.py', 'wire.jsonl', 'traffic.jsonl',
                         'events.jsonl', 'sent.jsonl')},
                audit_script_sha256=sha(Path(__file__).read_bytes()))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    args = parser.parse_args()
    result = audit(args.directory)
    with (args.directory/'offline-audit.json').open('x', encoding='utf8') as output:
        json.dump(result, output, ensure_ascii=False, indent=2)
        output.write('\n')
    print(json.dumps(result, ensure_ascii=False, indent=2))
