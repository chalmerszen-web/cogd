"""Bounded server-VAD observation: three synthetic inputs or six prepared PCM pairs.

One input per fresh connection; no device, microphone, tools or reply generation.
Raw receipts are evidence, not permission to act on a partial/final segment.
"""
import argparse
import base64
import hashlib
import json
from pathlib import Path
import queue
import threading
import time
import wave

from asr_manual_probe import MODEL, URL
from input_integrity import complete_input

CONFIG = dict(input_audio_format="pcm", sample_rate=16000,
              turn_detection=dict(type="server_vad", threshold=0.2, silence_duration_ms=700))
PREPARED_IDS = [f'{name}-{mode}' for name in ('greeting', 'correction', 'blue') for mode in ('raw', 'hp300')]


def sha(data):
    return hashlib.sha256(data).hexdigest()


def valid_session(session):
    """Validate selected settings; retain harmless provider extensions in the receipt."""
    vad = session.get("turn_detection")
    return (session.get("model") == MODEL and session.get("modalities") == ["text"]
            and session.get("input_audio_format") == "pcm" and session.get("sample_rate") == 16000
            and isinstance(vad, dict) and all(vad.get(k) == v for k, v in CONFIG["turn_detection"].items()))


def observe(out, inputs, execute, *, prepared=False, fixture_ids=None):
    out.mkdir(parents=True, exist_ok=False)
    script = Path(__file__).read_bytes()
    (out / "executed-script.py").write_bytes(script)
    origin = (inputs / "report.json").read_bytes()
    original = json.loads(origin)
    expected_ids = PREPARED_IDS if prepared else ["greeting", "correction", "greeting"]
    if fixture_ids is not None:
        if prepared or not 1 <= len(fixture_ids) <= 6 or any(not isinstance(i,str) or not i for i in fixture_ids):
            raise ValueError("Invalid explicit bounded fixture list")
        expected_ids = list(fixture_ids)
    if [f["id"] for f in original["fixtures"]] != expected_ids:
        raise ValueError("Unexpected fixture order or count")
    if prepared and (original.get('prepared_asr_inputs') != 1 or original.get('cloud_prerequisites_pass') is not True):
        raise ValueError('Prepared filter inputs have not passed offline prerequisites')
    (out / 'source-report.json').write_bytes(origin)
    cases, fixtures = [], []
    for fixture in original["fixtures"]:
        filename = fixture["input_file"]
        if Path(filename).name != filename:
            raise ValueError("Input filename must be local to the retained group")
        path = inputs / filename
        with wave.open(str(path), "rb") as wav:
            if (wav.getnchannels(), wav.getsampwidth(), wav.getframerate()) != (1, 2, 16000):
                raise ValueError("Expected PCM16 mono 16 kHz")
            original_pcm = wav.readframes(wav.getnframes())
        if len(original_pcm) != fixture["samples"] * 2 or sha(original_pcm) != fixture["pcm_sha256"]:
            raise ValueError("Retained fixture hash mismatch")
        if prepared and (fixture.get('extra_tail_ms') != 500 or
                         fixture.get('original_samples', 0) + 8000 != fixture['samples']):
            raise ValueError('Prepared source/tail accounting mismatch')
        pcm = original_pcm if prepared else original_pcm + bytes(700 * 32)
        if not 0 < len(pcm) <= 320000:
            raise ValueError("Input exceeds the device's ten-second bound")
        with wave.open(str(out / filename), "wb") as wav:
            wav.setparams((1, 2, 16000, 0, "NONE", "not compressed"))
            wav.writeframes(pcm)
        fixtures.append(dict(id=fixture["id"], expected=fixture["expected"], input_file=filename,
            samples=len(pcm) // 2, pcm_sha256=sha(pcm), retained_pcm_sha256=sha(original_pcm),
            original_source=fixture["source"], extra_tail_ms=fixture['extra_tail_ms'] if prepared else 700,
            original_samples=fixture['original_samples'] if prepared else len(original_pcm)//2))
        cases.append(pcm)
    report = dict(model=MODEL, url=URL, config=CONFIG, execute=execute, diagnostic=True,
        device_acceptance=False, observation_complete=False, attempts=0, retries=0, prepared_asr_inputs=1 if prepared else 0,
        script_sha256=sha(script), source_report_sha256=sha(origin), source_directory=str(inputs),
        chunk_samples=464, overall_seconds=120, fixtures=fixtures, trials=[],
        limitations=["Host synthetic playback data, not physical or human acceptance.",
                     "Missing input IDs remain unknown; no effects or output authority.",
                     "A final segment may precede later words or correction."])

    def save():
        (out / "report.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf8")

    save()
    if not execute:
        return report
    import websocket
    from qianwen_credentials import qianwen_key
    key = qianwen_key()
    began = time.monotonic()
    absolute = began + 120
    stamp = lambda: round(time.monotonic() - began, 6)
    try:
        for number, (fixture, pcm) in enumerate(zip(fixtures, cases), 1):
            trial = dict(round=number, id=fixture["id"], sent_samples=0, previews=[],
                         endpoints=[], commits=[], finals=[], completed=False)
            report["trials"].append(trial)
            pending = queue.Queue(maxsize=128)
            stopped = threading.Event()
            ws = worker = None
            nonce = 0
            received = 0

            def reader():
                try:
                    while not stopped.is_set():
                        raw = ws.recv()
                        pending.put((stamp(), raw), timeout=1)
                        if not raw:
                            return
                except Exception as exc:
                    if not stopped.is_set():
                        trial["receiver_error"] = type(exc).__name__
                        try:
                            pending.put((stamp(), None), timeout=1)
                        except queue.Full:
                            pass

            def log(direction, at, event):
                row = json.dumps(dict(round=number, direction=direction, observed_s=at, event=event), ensure_ascii=False)
                with (out / "traffic.jsonl").open("a", encoding="utf8") as file:
                    file.write(row.replace(key, "[REDACTED]") + "\n")

            def send(kind, **fields):
                nonlocal nonce
                if time.monotonic() >= absolute:
                    raise TimeoutError("Overall observation budget")
                nonce += 1
                event = dict(type=kind, event_id=f"asrvad_{number}_{nonce}", **fields)
                ws.send(json.dumps(event, ensure_ascii=False, separators=(",", ":")))
                if "audio" in event:
                    block = base64.b64decode(event.pop("audio"), validate=True)
                    event.update(samples=len(block) // 2, pcm_sha256=sha(block))
                log("send", stamp(), event)

            def receive(timeout):
                nonlocal received
                if time.monotonic() >= absolute:
                    raise TimeoutError("Overall observation budget")
                try:
                    at, raw = pending.get(timeout=timeout)
                except queue.Empty:
                    return None, None
                if not raw:
                    raise RuntimeError("Connection ended before session.finished")
                if not isinstance(raw, str) or len(raw.encode("utf8")) >= 6144 or received >= 512:
                    raise ValueError("Event budget or invalid message type")
                received += 1
                event = json.loads(raw)
                with (out / f"wire-{number:02}.jsonl").open("a", encoding="utf8") as file:
                    file.write(raw.replace(key, "[REDACTED]") + "\n")
                log("receive", at, event)
                if event.get("type") in ("error", "conversation.item.input_audio_transcription.failed"):
                    raise RuntimeError("Provider error; preserved in wire log")
                return at, event

            def wait(kind):
                deadline = min(absolute, time.monotonic() + 5)
                while time.monotonic() < deadline:
                    at, event = receive(.02)
                    if event is None:
                        continue
                    if event.get("type") != kind:
                        raise ValueError("Unexpected event at " + kind)
                    return at, event
                raise TimeoutError(kind)

            try:
                if time.monotonic() >= absolute:
                    raise TimeoutError("Overall observation budget")
                report["attempts"] += 1
                save()
                ws = websocket.create_connection(URL, header=["Authorization: Bearer " + key],
                    timeout=min(15, absolute - time.monotonic()), redirect_limit=0, enable_multithread=True)
                worker = threading.Thread(target=reader, daemon=True)
                worker.start()
                _, created = wait("session.created")
                trial["created"] = created["session"]
                if created["session"].get("model") != MODEL:
                    raise ValueError("Wrong ASR model")
                send("session.update", session=CONFIG)
                trial["ready_s"], updated = wait("session.updated")
                trial["updated"] = updated["session"]
                if not valid_session(updated["session"]):
                    raise ValueError("Wrong PCM/VAD configuration echo")
                trial["input_start_s"] = stamp()
                offset = 0
                finish_sent = False
                deadline = min(absolute, time.monotonic() + len(pcm) / 32000 + 10)
                while time.monotonic() < deadline:
                    at, event = receive(.003)
                    if event:
                        kind = event.get("type")
                        if kind == "session.finished":
                            if not finish_sent:
                                raise ValueError("Unsolicited session finish")
                            trial["session_finished_s"] = at
                            break
                        if kind in ("input_audio_buffer.speech_started", "input_audio_buffer.speech_stopped"):
                            field = "audio_start_ms" if kind.endswith("started") else "audio_end_ms"
                            value = event.get(field)
                            trial["endpoints"].append(dict(type=kind, at_s=at, item_id=event.get("item_id"),
                                **{field: value}, source_time_valid=(type(value) is int and 0 <= value <= offset / 32),
                                uploaded_samples=offset // 2, before_finish=not finish_sent))
                        elif kind == "input_audio_buffer.committed":
                            trial["commits"].append(dict(at_s=at, item_id=event.get("item_id")))
                        elif kind == "conversation.item.created":
                            if not isinstance(event.get("item"), dict):
                                raise ValueError("Invalid item announcement")
                        elif kind in ("conversation.item.input_audio_transcription.text", "conversation.item.input_audio_transcription.completed"):
                            final = kind.endswith("completed")
                            text = event.get("transcript") if final else event.get("text", "") + event.get("stash", "")
                            if not isinstance(text, str) or len(text.encode("utf8")) > 2048:
                                raise ValueError("Invalid transcript")
                            target = trial["finals" if final else "previews"]
                            if len(target) >= (16 if final else 128):
                                raise ValueError("Transcript event limit")
                            target.append(dict(at_s=at, text=text, item_id=event.get("item_id"),
                                uploaded_samples=offset // 2, before_finish=not finish_sent))
                        else:
                            raise ValueError("Unexpected event " + str(kind))
                    if offset < len(pcm) and stamp() >= trial["input_start_s"] + offset / 32000:
                        block = pcm[offset:offset + 928]
                        send("input_audio_buffer.append", audio=base64.b64encode(block).decode("ascii"))
                        offset += len(block)
                        trial["sent_samples"] = offset // 2
                    if offset == len(pcm) and not finish_sent:
                        send("session.finish")
                        trial["finish_sent_s"] = stamp()
                        finish_sent = True
                if "session_finished_s" not in trial:
                    raise TimeoutError("Missing session.finished")
                trial["combined_text"] = "".join(f["text"] for f in trial["finals"])
                trial["transcript_complete"] = complete_input(fixture["expected"], trial["combined_text"])
                trial["timed_endpoint_observed"] = any(e["type"].endswith("stopped") and e["source_time_valid"]
                                                       and e["before_finish"] for e in trial["endpoints"])
                trial["all_event_ids_present"] = bool(trial["endpoints"] and trial["commits"] and trial["finals"]) and all(
                    e.get("item_id") for e in trial["endpoints"] + trial["commits"] + trial["finals"])
                trial["completed"] = offset == len(pcm)
            finally:
                stopped.set()
                if ws is not None:
                    ws.close(timeout=2)
                if worker is not None:
                    worker.join(timeout=3)
                    if worker.is_alive():
                        raise RuntimeError("Receiver did not stop")
                trial["ended_s"] = stamp()
                save()
        report["observation_complete"] = len(report["trials"]) == len(cases) and all(t["completed"] for t in report["trials"])
        report["all_transcripts_complete"] = all(t["transcript_complete"] for t in report["trials"])
        report["timed_endpoints_observed"] = all(t["timed_endpoint_observed"] for t in report["trials"])
    except Exception as exc:
        report["error_type"] = type(exc).__name__
        report["reason"] = str(exc).replace(key, "[REDACTED]")[:180]
    finally:
        report["ended_s"] = stamp()
        save()
    return report


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--inputs", type=Path, default=Path("artifacts/voice-fast/standalone-asr-observe-01190-01"))
    parser.add_argument("--execute", action="store_true")
    parser.add_argument('--prepared', action='store_true', help='Use the six verified UX63 raw/HP pairs, without further padding')
    args = parser.parse_args()
    result = observe(args.out, args.inputs, args.execute, prepared=args.prepared)
    print(json.dumps({k: result.get(k) for k in ("observation_complete", "attempts", "all_transcripts_complete",
        "timed_endpoints_observed", "device_acceptance", "reason")}, ensure_ascii=False))
    if args.execute and not result["observation_complete"]:
        raise SystemExit(1)
