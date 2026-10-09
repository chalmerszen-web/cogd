"""Offline audit of a bounded standalone-ASR observation; never calls a service."""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import wave

from input_integrity import complete_input


def require(ok, message):
    if not ok:
        raise ValueError(message)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def audit(directory):
    report = json.loads((directory/'report.json').read_text(encoding='utf8'))
    wire = [json.loads(line) for line in (directory/'wire.jsonl').read_text(encoding='utf8').splitlines()]
    traffic = [json.loads(line) for line in (directory/'traffic.jsonl').read_text(encoding='utf8').splitlines()]
    require([r['event'] for r in traffic if r['direction'] == 'receive'] == wire, 'Wire/traffic mismatch')
    require(report['script_sha256'] == digest(directory/'executed-script.py'), 'Executed script hash')
    require(report['execute'] and report['attempts'] == 1 and report['retries'] == 0, 'Attempt accounting')
    require(len(report['turns']) == len(report['fixtures']) == 3, 'Three turns required')
    require(not report['accepted'] and not report['played'] and not report['tools_executed'], 'Observation scope')
    require(not report.get('reason'), 'An incomplete receipt is not a complete observation')
    inputs = []
    for fixture in report['fixtures']:
        source = Path(fixture['source'])
        require(digest(source) == fixture['source_file_sha256'], 'Source changed')
        with wave.open(str(directory/fixture['input_file']), 'rb') as wav:
            require((wav.getnchannels(), wav.getsampwidth(), wav.getframerate()) == (1, 2, 16000), 'Input format')
            pcm = wav.readframes(wav.getnframes())
        require(len(pcm) == fixture['samples']*2 and hashlib.sha256(pcm).hexdigest() == fixture['pcm_sha256'], 'PCM hash/count')
        inputs.append(pcm)
    current = -1
    sends = Counter()
    received = Counter()
    turns = []
    send_ids, receive_ids = set(), set()
    for row in traffic:
        event = row['event']
        kind = event['type']
        at = row['observed_s']
        if row['direction'] == 'send':
            sends[kind] += 1
            require(event['event_id'] not in send_ids, 'Duplicate outbound ID')
            send_ids.add(event['event_id'])
            if kind == 'session.update':
                require(current == -1 and sends[kind] == 1 and event['session'] == report['config'], 'Session update')
            elif kind == 'input_audio_buffer.append':
                if current == -1 or turns[current].get('final_s') is not None:
                    current += 1
                    require(current < 3, 'Extra input')
                    turns.append(dict(samples=0, chunks=0, previews=[]))
                turn = turns[current]
                require('commit_s' not in turn, 'Audio after commit')
                count = event['samples']
                require(0 < count <= report['chunk_samples'], 'Chunk size')
                offset = turn['samples']*2
                block = inputs[current][offset:offset+count*2]
                require(len(block) == count*2 and hashlib.sha256(block).hexdigest() == event['pcm_sha256'], 'Chunk replay/hash mismatch')
                turn['samples'] += count
                turn['chunks'] += 1
            elif kind == 'input_audio_buffer.commit':
                turn = turns[current]
                require('commit_s' not in turn and turn['samples']*2 == len(inputs[current]), 'Premature/duplicate commit')
                turn['commit_s'] = at
            elif kind == 'session.finish':
                require(current == 2 and 'final_s' in turns[current] and sends[kind] == 1, 'Premature finish')
            else:
                raise ValueError('Unexpected outbound action: '+kind)
        else:
            require(row['direction'] == 'receive', 'Direction')
            received[kind] += 1
            require(event['event_id'] not in receive_ids, 'Duplicate provider event ID')
            receive_ids.add(event['event_id'])
            if kind == 'session.created':
                require(event['session'] == report['created'], 'Created echo')
            elif kind == 'session.updated':
                require(event['session'] == report['updated'], 'Updated echo')
            elif kind == 'session.finished':
                require(sends['session.finish'] == 1 and at == report['server_finished_s'], 'Finish barrier')
            else:
                require(current >= 0, 'Input event without upload')
                turn = turns[current]
                require('final_s' not in turn, 'Input event after final')
                if kind == 'input_audio_buffer.committed':
                    require('commit_s' in turn and 'ack_s' not in turn, 'Commit ACK order')
                    turn.update(ack_s=at, commit_item_id=event.get('item_id'))
                elif kind == 'conversation.item.input_audio_transcription.text':
                    turn['previews'].append(dict(at_s=at, item_id=event.get('item_id'),
                        text=event['text']+event['stash'], uploaded_samples=turn['samples'], language=event.get('language')))
                elif kind == 'conversation.item.input_audio_transcription.completed':
                    require('ack_s' in turn and at >= turn['ack_s'], 'Final before ACK')
                    turn.update(final_s=at, transcript=event['transcript'], final_item_id=event.get('item_id'))
                else:
                    require(kind == 'conversation.item.created', 'Unexpected receive action')
    rows = []
    for turn, expected, fixture in zip(turns, report['turns'], report['fixtures']):
        require(turn['samples'] == expected['sent_samples'] == fixture['samples'], 'Input count')
        require(turn['previews'] == expected['previews'], 'Preview reconstruction')
        require(turn['transcript'] == expected['final_asr'], 'Final transcript mismatch')
        require(abs(turn['commit_s']-expected['commit_s']) < .020, 'Commit time')
        require(turn['ack_s'] == expected['commit_ack_s'] and turn['final_s'] == expected['final_asr_s'], 'ASR times')
        identity = bool(turn['commit_item_id'] and turn['commit_item_id'] == turn['final_item_id'])
        require(identity == expected['identity_verified'], 'Invented identity')
        complete = complete_input(fixture['expected'], turn['transcript'])
        require(complete == expected['complete_input'], 'Transcript completeness')
        term = '请把灯' if fixture['id'] == 'correction' else '介绍'
        first = next((p for p in turn['previews'] if term in p['text']), None)
        rows.append(dict(id=fixture['id'], samples=turn['samples'], chunks=turn['chunks'],
            previews=len(turn['previews']), transcript=turn['transcript'], complete_input=complete,
            identity_verified=identity, commit_to_final_ms=round((turn['final_s']-turn['commit_s'])*1000),
            keyword=term, keyword_before_commit_ms=round((turn['commit_s']-first['at_s'])*1000) if first else None))
    require(received['session.finished'] == 1 and len(rows) == 3, 'Session termination')
    require(report['complete'] == all(row['identity_verified'] and row['complete_input'] for row in rows), 'Acceptance status changed')
    result = dict(audit_complete=True, protocol_accepted=report['complete'], device_accepted=False,
        scope='offline_evidence_consistency_only', sends=dict(sends), receives=dict(received), turns=rows,
        duration_s=report['finished_s'], scripts=dict(probe=report['script_sha256'], audit=digest(Path(__file__))),
        evidence={name:digest(directory/name) for name in ('report.json', 'wire.jsonl', 'traffic.jsonl')})
    with (directory/'offline-audit.json').open('x', encoding='utf8') as out:
        json.dump(result, out, ensure_ascii=False, indent=2)
        out.write('\n')
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    args = parser.parse_args()
    result = audit(args.directory)
    print(json.dumps(result, ensure_ascii=False, indent=2))
