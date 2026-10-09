"""Audit retained VAD observations, including all transmitted PCM block hashes."""
import argparse
import hashlib
import json
from pathlib import Path
import wave

from asr_vad_probe import MODEL, PREPARED_IDS, valid_session
from input_integrity import complete_input


def sha(data):
    return hashlib.sha256(data).hexdigest()


def require(condition, reason):
    if not condition:
        raise ValueError(reason)


def audit(directory, *, observe_failures=False):
    target = directory / "offline-audit.json"
    if target.exists():
        raise FileExistsError(target)
    report_bytes = (directory / "report.json").read_bytes()
    report = json.loads(report_bytes)
    traffic_bytes = (directory / "traffic.jsonl").read_bytes()
    traffic = [json.loads(line) for line in traffic_bytes.splitlines()]
    count = 6 if report.get('prepared_asr_inputs') == 1 else 3
    if count == 6:
        require([f['id'] for f in report['fixtures']] == PREPARED_IDS, 'Wrong paired input order')
    require(not observe_failures or count == 6, 'Failure observation is limited to the prepared paired study')
    require(report["observation_complete"] and report["attempts"] == count and report["retries"] == 0,
            "Not the completed single bounded observation")
    require(sha((directory / "executed-script.py").read_bytes()) == report["script_sha256"], "Executed script hash")
    require(len(report["fixtures"]) == len(report["trials"]) == count, "Missing trial")
    result = dict(evidence_consistent=True, device_acceptance=False, observe_failures=observe_failures,
        source_report_sha256=sha(report_bytes),
        traffic_sha256=sha(traffic_bytes), script_sha256=sha(Path(__file__).read_bytes()), trials=[],
        limitations=["Checks evidence consistency, not physical timing or robustness in microphone noise.",
                     "Speech-stopped timestamps are provider segment boundaries, not human turn completion.",
                     "The correction has multiple final segments; no segment alone authorizes an action."])
    for number, (fixture, trial) in enumerate(zip(report["fixtures"], report["trials"]), 1):
        require(trial["round"] == number and trial["completed"], "Incomplete trial")
        require(Path(fixture["input_file"]).name == fixture["input_file"], "Input path")
        with wave.open(str(directory / fixture["input_file"]), "rb") as wav:
            require((wav.getframerate(), wav.getnchannels(), wav.getsampwidth()) == (16000, 1, 2), "PCM format")
            pcm = wav.readframes(wav.getnframes())
        require(sha(pcm) == fixture["pcm_sha256"] and len(pcm) == fixture["samples"] * 2, "Fixture hash")
        rows = [r for r in traffic if r["round"] == number]
        require(rows, "No traffic")
        raw_bytes = (directory / f"wire-{number:02}.jsonl").read_bytes()
        require([json.loads(line) for line in raw_bytes.splitlines()] ==
                [r["event"] for r in rows if r["direction"] == "receive"], "Raw receipts disagree")
        offset = 0
        sent_update = sent_finish = created = updated = finished = False
        session_id = None
        segments = {}
        seen_event_ids = set()
        previews, commits, starts, stops, finals = 0, 0, 0, 0, []
        for row in rows:
            e = row["event"]
            kind = e["type"]
            require(isinstance(e.get("event_id"), str) and e["event_id"] not in seen_event_ids, "Duplicate/missing event ID")
            seen_event_ids.add(e["event_id"])
            if row["direction"] == "send":
                if kind == "session.update":
                    require(created and not sent_update and not updated, "Update ordering")
                    require(e["session"] == report["config"], "Requested settings changed")
                    sent_update = True
                elif kind == "input_audio_buffer.append":
                    require(updated and not sent_finish, "Append ordering")
                    count = e["samples"]
                    require(count == min(464, (len(pcm) - offset) // 2) and count > 0, "Missing/oversize chunk")
                    block = pcm[offset:offset + count * 2]
                    require(sha(block) == e["pcm_sha256"], "Transmitted PCM hash")
                    offset += count * 2
                elif kind == "session.finish":
                    require(updated and not sent_finish and offset == len(pcm), "Finish before full input")
                    sent_finish = True
                else:
                    raise ValueError("Unexpected send: " + kind)
                continue
            require(row["direction"] == "receive" and not finished, "Invalid receive ordering")
            if kind == "session.created":
                require(not created and e["session"]["model"] == MODEL, "Wrong created session")
                session_id = e["session"]["id"]
                created = True
            elif kind == "session.updated":
                require(sent_update and not updated and valid_session(e["session"]) and
                        e["session"]["id"] == session_id, "Wrong update echo")
                updated = True
            elif kind == "session.finished":
                require(sent_finish, "Unsolicited finish")
                finished = True
            else:
                require(updated and offset > 0, "Input event before upload")
                item_id = e.get("item_id") if kind != "conversation.item.created" else e["item"].get("id")
                require(isinstance(item_id, str) and item_id, "Unbound input event")
                if kind == "input_audio_buffer.speech_started":
                    ms = e["audio_start_ms"]
                    require(item_id not in segments and type(ms) is int and 0 <= ms <= offset / 32, "Invalid start")
                    segments[item_id] = dict(item_id=item_id, start_ms=ms, started_s=row["observed_s"])
                    starts += 1
                    continue
                require(item_id in segments, "Unknown segment")
                segment = segments[item_id]
                if kind == "conversation.item.created":
                    require(not segment.get("announced"), "Duplicate announcement")
                    segment["announced"] = True
                elif kind == "input_audio_buffer.speech_stopped":
                    ms = e["audio_end_ms"]
                    require("end_ms" not in segment and type(ms) is int and segment["start_ms"] <= ms <= offset / 32,
                            "Invalid end timestamp")
                    segment.update(end_ms=ms, stopped_s=row["observed_s"], stopped_before_finish=not sent_finish)
                    stops += 1
                elif kind == "input_audio_buffer.committed":
                    require("end_ms" in segment and not segment.get("committed"), "Invalid automatic commit")
                    segment["committed"] = True
                    commits += 1
                elif kind == "conversation.item.input_audio_transcription.text":
                    require("text" not in segment, "Partial after final")
                    previews += 1
                elif kind == "conversation.item.input_audio_transcription.completed":
                    require(segment.get("committed") and "text" not in segment, "Invalid final segment")
                    require(isinstance(e.get("transcript"), str) and (e["transcript"] or observe_failures), "Empty final")
                    segment.update(text=e["transcript"], final_s=row["observed_s"], final_before_finish=not sent_finish)
                    finals.append(segment)
                else:
                    raise ValueError("Unexpected receive: " + kind)
        require(finished and offset == len(pcm) and starts == stops == commits == len(finals), "Unfinished segments")
        combined = "".join(s["text"] for s in finals)
        text_complete = complete_input(fixture["expected"], combined)
        require(combined == trial["combined_text"] and text_complete == trial['transcript_complete'], 'Text report mismatch')
        require(text_complete or observe_failures, "Incomplete final text")
        require(trial["sent_samples"] == offset // 2 and len(trial["previews"]) == previews, "Report count mismatch")
        require([s["text"] for s in finals] == [s["text"] for s in trial["finals"]], "Reported finals differ")
        result["trials"].append(dict(round=number, samples=offset // 2, previews=previews, segments=finals,
            combined_text=combined, transcript_complete=text_complete,
            empty_final_segments=sum(not s['text'] for s in finals),
            identities_consistent=True, wire_sha256=sha(raw_bytes),
            finished_after_last_final_ms=round((trial["session_finished_s"] - finals[-1]["final_s"]) * 1000)))
    require(sha((directory / "report.json").read_bytes()) == sha(report_bytes), "Report changed during audit")
    target.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf8")
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument('--observe-failures', action='store_true', help='Audit paired-study evidence while retaining empty/incomplete text as failures')
    args = parser.parse_args()
    result = audit(args.directory, observe_failures=args.observe_failures)
    print(json.dumps(dict(evidence_consistent=result["evidence_consistent"], device_acceptance=False,
                         segments=[len(t["segments"]) for t in result["trials"]]), ensure_ascii=False))
