"""Three continuous wake/voice turns with deliberately short post-turn gaps.

This is a diagnostic script.  It keeps one external Recorder for the whole
group, never toggles voice between turns, and preserves failed wake attempts.
It does not wait for fast_ready after the first turn: the next wake is played
about 250 ms after the previous @done so listener pause/rearm windows remain
visible.
"""
import argparse
import hashlib
import json
from pathlib import Path
import time

from device import Device, ROOT
from record_speaker import Recorder
from wave_play import play
from conversation_test import leveled
from input_integrity import complete_input
from memory_evidence import memory_contains, memory_effect
from fixture_acceptance import check_content, check_runtime_health
from export_audio_clip import export_clip, retain_turn_clip


WAKE_MANIFEST = ROOT / "artifacts/kws-phase6/replay-selection.jsonl"
PROMPT_TEXT = "请用一句话介绍你自己。"
MAX_GROUP_SECONDS = 235


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def read_wakes():
    rows = [json.loads(line) for line in WAKE_MANIFEST.read_text(encoding="utf8").splitlines()]
    chosen = {}
    for language in ("zh", "yue"):
        chosen[language] = next(row for row in rows if row["label"] == 1 and row["language"] == language)
    return chosen


def save(report, out):
    (out / "report.json").write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf8")


def snapshot(device):
    return {
        "wake": device.command("agent wake status", query=True, timeout=8),
        "voice": device.command("agent voice status", query=True, timeout=8),
    }


def drain_events(device, trial):
    for line in device.lines():
        trial["raw_lines"].append(line)
        if line == "@done" or line.startswith("@error"):
            trial["terminal"] = line
            trial["terminal_monotonic"] = time.monotonic()
        if trial.get('probe_ack'):
            probes=trial.setdefault('ack_overlap_probes',[])
            if line.startswith('@voice '):
                event=json.loads(line[7:])
                stage=event.get('stage')
                planning=event.get('request')==1 and stage in ('llm_plan_select_begin','llm_plan_sent')
                spoken=(stage=='progress_playback' and any(p['stage']=='llm_plan_sent' for p in probes) and
                        not any(p['stage']=='progress_playback' for p in probes))
                if planning or spoken:
                    probes.append(dict(stage=stage,event_time_ms=event['time_ms'],
                                       requested_monotonic=time.monotonic()))
                    # Send without consuming any voice events or blocking the
                    # stream. The ordinary USB owner answers this read-only query.
                    device.send('agent audio status')
            elif line.startswith('{'):
                value=json.loads(line)
                if isinstance(value,dict) and 'audio_ready' in value:
                    pending=next((p for p in probes if 'response' not in p),None)
                    if pending is not None:
                        pending.update(response=value,observed_monotonic=time.monotonic())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--mode", choices=("fast", "classic"), default="fast")
    parser.add_argument("--firmware")
    parser.add_argument('--max-attempts', type=int, choices=(1, 2, 3), default=3,
                        help='Preserve at most this many wake attempts per turn; use1 for a fixed no-retry group')
    parser.add_argument("--asr-model", help="Require this device-reported ASR model before playback")
    parser.add_argument("--prompt-id",default="greeting")
    parser.add_argument('--prompt-gain',type=float,default=.35,help='Input playback gain, .1.. .35; wake volume stays unchanged')
    parser.add_argument("--prompt-manifest",type=Path,default=ROOT / "artifacts/voice-cloud/prompts-01/manifest.json")
    parser.add_argument("--preconnect",choices=("capture","idle"))
    parser.add_argument("--prefetch",choices=("on","off"))
    parser.add_argument("--reuse",choices=("on","off"))
    parser.add_argument('--capture-output',choices=('hold','off'))
    parser.add_argument('--tx-power', type=int, help='Temporary Wi-Fi limit, quarter-dBm units (8..84)')
    parser.add_argument('--probe-ack',action='store_true',help='Read speaker state during first DeepSeek preparation/body completion')
    parser.add_argument('--heap-session',action='store_true',
                        help='Diagnostic only: one bounded allocation session spanning turns and closing commands')
    parser.add_argument('--memory-fact',help='Verify the requested durable fact before/after, tracking any writes between turns')
    parser.add_argument('--endpoint-trace',action='store_true',help='Export classification after capture; diagnostic timing only')
    parser.add_argument('--prewake-quiet-ms',type=int,default=0,
                        help='Diagnostic only: wait0..3000ms before each wake, excludes rapid-rearm acceptance')
    parser.add_argument('--capture-evidence',action='store_true',help='Per-turn read-only stats and final clip; diagnostic gaps only')
    parser.add_argument('--stack-evidence',action='store_true',help='Per-turn network/TTS stack watermarks; read-only queries add diagnostic gaps')
    parser.add_argument('--retain-failed-capture',action='store_true',
                        help='Diagnostic: pause wake after an incomplete input, export its clip, then resume; no rapid-rearm acceptance')
    parser.add_argument('--retain-all-captures',action='store_true',
                        help='Diagnostic: retain every committed turn clip before overwrite; no latency/rearm acceptance')
    parser.add_argument('--check-content', choices=('greeting', 'correction', 'blue'),
                        help='Additional assertions for a fixed fixture; not subjective listening')
    args = parser.parse_args()
    if not 0 <= args.prewake_quiet_ms <= 3000:
        parser.error('--prewake-quiet-ms must be0..3000')
    if args.retain_failed_capture or args.retain_all_captures:
        args.capture_evidence = True
    if not .1 <= args.prompt_gain <= .35:
        parser.error('--prompt-gain must be .1.. .35')
    if args.tx_power is not None and not 8 <= args.tx_power <= 84:
        parser.error('--tx-power must be 8..84')
    args.out.mkdir(parents=True, exist_ok=False)
    wakes = read_wakes()
    report = {
        "complete": False,
        "diagnostic": True,
        "mode": args.mode,
        "max_attempts_per_turn": args.max_attempts,
        "prompt_gain": args.prompt_gain,
        "prompt": PROMPT_TEXT,
        "prompt_sha256": hashlib.sha256(PROMPT_TEXT.encode("utf8")).hexdigest(),
        "wake_manifest": str(WAKE_MANIFEST),
        "round_languages": ["zh", "yue", "zh"],
        "trials": [],
        "started": time.time(),
        "script_sha256": sha256(Path(__file__)),
        "runtime_checker_sha256": sha256(Path(__file__).with_name('fixture_acceptance.py')),
    }
    if args.check_content:
        report['content_checker_sha256'] = sha256(Path(__file__).with_name('fixture_acceptance.py'))
    device = Device(args.out)
    group_deadline = time.monotonic() + MAX_GROUP_SECONDS
    recorder = None
    try:
        prompt_manifest = json.loads(args.prompt_manifest.read_text(encoding="utf-8-sig"))
        report['prompt_manifest_sha256']=sha256(args.prompt_manifest)
        prompt_row = next(row for row in prompt_manifest["prompts"] if row["id"] == args.prompt_id)
        report['prompt']=prompt_row['text']
        if args.memory_fact:
            if args.memory_fact not in report['prompt']:
                raise ValueError('Expected memory fact is absent from the selected input prompt')
            report['expected_memory_fact']=args.memory_fact
        report['prompt_sha256']=hashlib.sha256(prompt_row['text'].encode('utf8')).hexdigest()
        prompt_source = ROOT / prompt_row["path"]
        prompt_path = args.out / "prompt.wav"
        prompt_meta = leveled(prompt_source, prompt_path)
        wake_meta = {}
        for language, row in wakes.items():
            target = args.out / ("wake-%s.wav" % language)
            wake_meta[language] = dict(row=row, leveled=leveled(ROOT / row["path"], target), path=str(target))
        report["prompt_source"] = dict(prompt_row, leveled=prompt_meta, path=str(prompt_path))
        report["wake_sources"] = wake_meta
        save(report, args.out)
        device.command("agent voice off", timeout=8)
        memory_known=False
        if args.memory_fact:
            report['memory_before']=device.command('agent context summary',query=True,timeout=8)
            memory_known=memory_contains(report['memory_before'],args.memory_fact)
        if args.endpoint_trace:
            device.command('agent voice endpoint-trace on',timeout=8)
            report['endpoint_trace_enabled']=True
            report['latency_acceptance_eligible']=False
        if args.prewake_quiet_ms:
            report['prewake_quiet_ms']=args.prewake_quiet_ms
            report['rearm_acceptance_eligible']=False
            report['latency_acceptance_eligible']=False
        if args.capture_evidence:
            report['capture_evidence']=True
            report['latency_acceptance_eligible']=False
            report['rearm_acceptance_eligible']=False
        if args.stack_evidence:
            report['stack_evidence']=True
            report['latency_acceptance_eligible']=False
            report['rearm_acceptance_eligible']=False
        if args.retain_failed_capture or args.retain_all_captures:
            report['retain_failed_capture']=args.retain_failed_capture
            report['retain_all_captures']=args.retain_all_captures
            report['capture_exporter_sha256']=sha256(Path(__file__).with_name('export_audio_clip.py'))
        report['before']=device.command('agent status',query=True,timeout=8)
        if args.firmware and report['before']['version']!=args.firmware:raise ValueError('Wrong firmware')
        if args.tx_power is not None:
            report['radio_before'] = report['before']
            if 'wifi_tx_quarter_dbm' not in report['before']:
                raise RuntimeError('Firmware lacks radio diagnostic')
            device.command('agent wifi power ' + str(args.tx_power), timeout=8)
            report['before'] = device.command('agent status', query=True, timeout=8)
            if report['before']['wifi_tx_quarter_dbm'] != args.tx_power:
                raise RuntimeError('Temporary radio setting did not apply exactly')
        if args.preconnect:device.command('agent voice preconnect '+args.preconnect,timeout=8)
        if args.prefetch:
            device.command('agent voice prefetch '+args.prefetch,timeout=8)
            report['prefetch']=args.prefetch
            applied=device.command('agent voice prefetch status',query=True,timeout=8)
            if applied.get('prefetch')!=(args.prefetch=='on'):raise RuntimeError('Prefetch setting did not apply')
        if args.reuse:
            device.command('agent voice reuse '+args.reuse,timeout=8)
            report['reuse']=args.reuse
            applied=device.command('agent voice reuse status',query=True,timeout=8)
            if applied.get('reuse')!=(args.reuse=='on'):raise RuntimeError('Reuse setting did not apply')
        if args.capture_output:
            device.command('agent voice capture-output '+args.capture_output,timeout=8)
            report['capture_output']=device.command('agent wake status',query=True,timeout=8)
            if report['capture_output'].get('capture_output_hold')!=(args.capture_output=='hold'):
                raise RuntimeError('Capture output setting did not apply')
        device.command("agent voice mode " + args.mode, timeout=8)
        if args.heap_session:
            device.command('agent voice heap begin',timeout=8)
            report['heap_session_started']=True
            report['latency_acceptance_eligible']=False
            report['rearm_acceptance_eligible']=False
        device.command("agent voice on", timeout=8)

        # First round may wait for ordinary listener admission; fast_ready is
        # intentionally not part of this gate because cold capture is valid.
        ready_deadline = time.monotonic() + 15
        while time.monotonic() < ready_deadline:
            state = snapshot(device)
            if state["wake"].get("state") == "listening":
                if args.asr_model and state["voice"].get("asr_model") != args.asr_model:
                    raise ValueError("Wrong ASR backend for this group")
                report["initial_state"] = state
                break
            time.sleep(.1)
        else:
            raise TimeoutError("initial wake listener did not become listening")

        recorder = Recorder(args.out / "speaker.wav", seconds=220)
        recorder.__enter__()
        report['recorder_ready_monotonic']=recorder.started
        report['recorder_ready_note']='Recorder readiness is not WAV zero; use template alignment.'
        previous_terminal = None
        for index, language in enumerate(("zh", "yue", "zh"), 1):
            if time.monotonic() >= group_deadline:
                raise TimeoutError("continuous group exceeded four minute bound")
            row = wakes[language]
            trial_dir = args.out / ("trial-%02d" % index)
            trial_dir.mkdir()
            wake_meta = report["wake_sources"][language]
            trial = {
                "round": index,
                "language": language,
                "wake_clip_id": row["clip_id"],
                "wake_source": row,
                "wake_meta": wake_meta,
                "prompt_text": report['prompt'],
                "prompt_sha256": report["prompt_sha256"],
                "attempts": [],
                "events": [],
                "raw_lines": [],
            }
            report["trials"].append(trial)
            if index > 1:
                target = previous_terminal + .25 if previous_terminal is not None else time.monotonic()
                time.sleep(max(0, target - time.monotonic()))
            for attempt_no in range(1, args.max_attempts + 1):
                attempt = {
                    "attempt": attempt_no,
                    "started_monotonic": time.monotonic(),
                    "before": None,
                    "events": [],
                    "raw_lines": [],
                    "triggered": False,
                    "prompt_played": False,
                    "probe_ack": args.probe_ack,
                }
                trial['attempts'].append(attempt)
                save(report,args.out)
                event_start = len(device.events)
                if index == 1 and attempt_no == 1:
                    attempt["before"] = snapshot(device)
                if args.prewake_quiet_ms:
                    time.sleep(args.prewake_quiet_ms/1000)
                    attempt['prewake_clock']=device.command('agent voice noise-clock',query=True,timeout=5)
                attempt["wake_started_monotonic"] = time.monotonic()
                playback = play(wake_meta["path"], gain=.35)
                attempt["wake_playback"] = playback
                attempt["wake_sha256"] = sha256(wake_meta["path"])
                if previous_terminal is not None and playback.get("source_started") is not None:
                    attempt["actual_gap_ms"] = (playback["source_started"] - previous_terminal) * 1000
                deadline = min(group_deadline, time.monotonic() + 2)
                while time.monotonic() < deadline:
                    drain_events(device, attempt)
                    state = snapshot(device)
                    attempt["last_state"] = state
                    if state["wake"].get("state") == "recording":
                        attempt["triggered"] = True
                        break
                    time.sleep(.08)
                attempt["wake_ended_monotonic"] = time.monotonic()
                if attempt["triggered"]:
                    attempt["prompt_source"] = prompt_meta
                    attempt["prompt_playback"] = play(prompt_path, gain=args.prompt_gain)
                    attempt["prompt_sha256"] = sha256(prompt_path)
                    attempt["prompt_played"] = True
                    terminal_deadline = min(group_deadline, time.monotonic() + 60)
                    while time.monotonic() < terminal_deadline and "terminal" not in attempt:
                        drain_events(device, attempt)
                        if 'terminal' not in attempt:time.sleep(.01)
                    attempt["terminal"] = attempt.get("terminal")
                    if attempt["terminal"]:
                        previous_terminal = attempt["terminal_monotonic"]
                        attempt["events"] = device.events[event_start:]
                        break
                attempt["events"] = device.events[event_start:]
                attempt["after"] = snapshot(device)
                # Keep the failure and try again without voice off/on.  A
                # short bounded gap prevents an accidental long ready wait.
                if attempt_no < args.max_attempts:
                    time.sleep(.1)
            trial["events"] = [event for attempt in trial["attempts"] for event in attempt["events"]]
            trial["raw_lines"] = [line for attempt in trial["attempts"] for line in attempt["raw_lines"]]
            trial["complete"] = any(a["prompt_played"] and a.get("terminal") == "@done" for a in trial["attempts"])
            texts=[e.get('text','') for e in trial['events'] if e.get('stage')=='asr_text']
            trial['asr']=texts[-1] if texts else ''
            trial['input_checks']={word:word in trial['asr'] for word in prompt_row.get('required',[])}
            trial['core_task_complete']=trial['complete'] and all(trial['input_checks'].values())
            if args.memory_fact:
                memory_known=memory_effect(trial['raw_lines'],args.memory_fact,memory_known)
                trial['memory_effect_verified']=memory_known
                trial['core_task_complete'] &= trial['memory_effect_verified']
            trial['input_complete']=complete_input(report['prompt'],trial['asr'])
            trial['complete']=trial['core_task_complete'] and trial['input_complete']
            if args.check_content:
                if args.check_content in ('blue','correction'):
                    trial['light_readback']=device.command('agent light get',query=True,timeout=8)
                trial['protocol_complete'] = trial['complete']
                trial['content_check'] = check_content(trial, args.check_content)
                trial['complete'] &= trial['content_check']['passed']
            trial['functional_complete']=trial['complete']
            trial['runtime_health']=check_runtime_health(trial['raw_lines'])
            trial['complete'] &= trial['runtime_health']['passed']
            if args.capture_evidence:
                trial['wake_after']=device.command('agent wake status',query=True,timeout=8)
                trial['audio_after']=device.command('agent audio status',query=True,timeout=8)
            if args.stack_evidence:
                trial['stack_after']={
                    'system':device.command('agent status',query=True,timeout=8),
                    'voice':device.command('agent voice status',query=True,timeout=8)}
            if args.retain_all_captures or (args.retain_failed_capture and not trial['input_complete']):
                # Persist the original failure before any diagnostic operation.
                # A failed export aborts this group with the clip still protected.
                if (trial['audio_after']['clip_ready'] and trial['wake_after'].get('error')=='ok' and
                    any(e['stage']=='vad_end' for e in trial['events'])):
                    trial['retained_clip_path']=str(trial_dir/'retained-device-clip')
                    save(report,args.out)
                    trial['retained_clip']=retain_turn_clip(device,trial_dir/'retained-device-clip',
                                                           expected_wake=trial['wake_after'])
                else:
                    trial['retention_skipped']='No committed current-turn clip; old data must not be attributed to this input'
            save(report, args.out)
            print(json.dumps(dict(round=index,complete=trial['complete'],core_task_complete=trial['core_task_complete'],
                input_complete=trial['input_complete'],asr=trial['asr'],runtime_health=trial['runtime_health'],
                attempts=[a['triggered'] for a in trial['attempts']]),ensure_ascii=False),flush=True)
        report['run_completed']=len(report['trials'])==3
        report['conversations_complete']=all(trial['complete'] for trial in report['trials'])
        report['functional_conversations_complete']=all(t['functional_complete'] for t in report['trials'])
        report['runtime_health_passed']=all(t['runtime_health']['passed'] for t in report['trials'])
        report['core_tasks_complete']=all(trial['core_task_complete'] for trial in report['trials'])
        report['all_first_wakes_hit']=all(t['attempts'] and t['attempts'][0]['triggered'] for t in report['trials'])
        report['complete']=report['run_completed'] and report['conversations_complete'] and report['all_first_wakes_hit']
        report['wake_attempt_count']=sum(len(t['attempts']) for t in report['trials'])
        report['after']=device.command('agent status',query=True,timeout=8)
        report['wake_after']=device.command('agent wake status',query=True,timeout=8)
    except Exception as error:
        report['error']=type(error).__name__+': '+str(error)
        raise
    finally:
        complete_before_cleanup=report.get('complete',False)
        if args.memory_fact:
            report['memory_readback_verified']=False
            report['complete']=False
        try:
            try:
                device.command("agent voice off", timeout=8)
                if args.endpoint_trace:device.command('agent voice endpoint-trace off',timeout=8)
                if args.capture_output:device.command('agent voice capture-output hold',timeout=8)
                if args.memory_fact and report.get('run_completed'):
                    memory=device.command('agent context summary',query=True,timeout=8)
                    report['memory_readback']=memory
                    report['memory_readback_verified']=memory_contains(memory,args.memory_fact)
                    report['complete']=complete_before_cleanup and report['memory_readback_verified']
            except Exception as error:
                report.setdefault("cleanup_errors", []).append(str(error))
            if 'radio_before' in report:
                try:
                    device.command('agent wifi power ' + str(report['radio_before']['wifi_tx_quarter_dbm']), timeout=8)
                    report['radio_restored'] = device.command('agent status', query=True, timeout=8)
                    if report['radio_restored']['wifi_tx_quarter_dbm'] != report['radio_before']['wifi_tx_quarter_dbm']:
                        raise RuntimeError('Radio restoration mismatch')
                except Exception as error:
                    report.setdefault('cleanup_errors', []).append('radio: ' + str(error))
            if report.get('heap_session_started'):
                try:
                    # Cover asynchronous model unload/workspace restoration
                    # and ordinary closing queries, with no acceptance wait.
                    device.command('agent wake off',timeout=8)
                    time.sleep(3)
                    report['post_cleanup_status']=device.command('agent status',query=True,timeout=8)
                    event_start=len(device.events)
                    device.command('agent voice heap end',timeout=8)
                    report['heap_session_events']=device.events[event_start:]
                    report['heap_session_ended']=True
                except Exception as error:
                    report.setdefault('cleanup_errors',[]).append('heap session: '+str(error))
        finally:
            if recorder is not None:
                try:recorder.__exit__(None, None, None)
                except Exception as error:report.setdefault('cleanup_errors',[]).append(str(error))
            if args.capture_evidence and report.get('run_completed'):
                try:report['last_device_clip']=export_clip(device,args.out/'last-device-clip')
                except Exception as error:report.setdefault('cleanup_errors',[]).append('clip export: '+str(error))
            report["ended"] = time.time()
            report["external_recording"] = {
                "path": str(args.out / "speaker.wav"),
                "sha256": sha256(args.out / "speaker.wav") if (args.out / "speaker.wav").exists() else None,
            }
            save(report, args.out)
            device.save()
            device.close()
    print(json.dumps({"complete": report["complete"], "rounds": len(report["trials"])}, ensure_ascii=False))
    if not report["complete"]:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
