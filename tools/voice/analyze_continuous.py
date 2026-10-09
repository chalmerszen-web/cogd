"""Offline multi-turn acoustic analysis: local files/model only, no USB or network.

Fits repeated prompt correlations to the recorded waveOut time intervals. WAV
zero is inferred from that joint fit, never from missing Recorder timestamps.
Energy onsets remain candidates; a local ASR transcript identifies the content
but cannot certify the first useful phoneme or human listening quality.
"""
import argparse
import json
from pathlib import Path

import numpy as np
from scipy import ndimage, signal
import soundfile as sf

from analyze_latency import correlate, envelope, load, sha
from endpoint_cue import find_endpoint_cue

ROOT = Path(__file__).resolve().parents[2]
RATE = 16000
BANDS = [(300, 1000), (1000, 2000), (2000, 3500)]


def answer_candidate(attempt, source_match, onsets, text, input_end):
    """A partial/error turn or punctuation is not evidence of an answer."""
    compact = ''.join(c for c in text if c.isalnum())
    if attempt.get('terminal') != '@done':
        return dict(status='unknown', reason='Turn did not complete; partial audio retained, no answer latency claim')
    if not source_match:
        return dict(status='unknown', reason='This input copy failed the fixed waveform gates; transcript retained but no latency claim')
    if not compact or compact in ('收到', '收到我来处理', '收到等我处理') or not onsets:
        return dict(status='unknown', reason='No non-acknowledgement speech transcript and sustained final onset')
    return dict(status='candidate', acoustic_answer_onset_candidate_s=onsets[0],
                acoustic_answer_latency_candidate_s=onsets[0]-input_end)


def align_repeated(source, recording, host_starts):
    # WAV padding is not acoustic input. A loud endpoint cue can overlap a
    # silent suffix and dominate the normalized correlation denominator.
    # Remove exact zeros only, with10ms margins; retain even one-LSB phonemes.
    # Translate every fitted time back to the original playback sample clock.
    nonzero = np.flatnonzero(source != 0)
    if not len(nonzero):
        return dict(accepted=False, reason='Silent source has no alignment evidence')
    begin = max(0, int(nonzero[0])-RATE//100)
    end = min(len(source), int(nonzero[-1])+1+RATE//100)
    reference_samples = [begin, end]
    reference_offset = begin/RATE
    source = source[begin:end]
    relative = np.asarray(host_starts)-host_starts[0]
    limit = len(recording)/RATE-len(source)/RATE-float(relative[-1])
    if len(relative) < 2 or limit < 0 or np.any(np.diff(relative) <= 0):
        return dict(accepted=False, reason='Need multiple ordered prompts fitting the recording')
    curves, envelopes = [], []
    for lo, hi in BANDS:
        sos = signal.butter(4, [lo, hi], fs=RATE, btype='bandpass', output='sos')
        x = signal.sosfiltfilt(sos, source)
        y = signal.sosfiltfilt(sos, recording)
        curves.append(correlate(x, y))
        envelopes.append(correlate(envelope(x), envelope(y)))
    # Joint hypotheses must explain all copies with one clock offset. A small
    # local tolerance admits independent waveOut scheduling jitter, not a free
    # choice of the strongest repeated peak for each turn.
    grid = np.arange(0, limit, .01)
    scores = []
    for curve in curves[1:]:
        nearby = ndimage.maximum_filter1d(curve, 2*round(.03*RATE)+1)
        scores.append(np.mean([nearby[np.round((grid+dt)*RATE).astype(int)] for dt in relative], axis=0))
    score = np.mean(scores, axis=0)
    best = int(np.argmax(score))
    competing = score.copy()
    competing[abs(grid-grid[best]) <= .25] = -1
    second = int(np.argmax(competing))
    rows = []
    for index, dt in enumerate(relative):
        predicted = float(grid[best]+dt)
        bands = []
        for band, curve, env in zip(BANDS, curves, envelopes):
            lo = max(0, round((predicted-.04)*RATE))
            hi = min(len(curve), round((predicted+.04)*RATE)+1)
            at = lo+int(np.argmax(curve[lo:hi]))
            elo, ehi = max(0, round((predicted-.06)*100)), min(len(env), round((predicted+.06)*100)+1)
            eat = elo+int(np.argmax(env[elo:ehi]))
            bands.append(dict(band_hz=band, start_s=at/RATE-reference_offset, correlation=float(curve[at]),
                              envelope_start_s=eat/100-reference_offset, envelope_correlation=float(env[eat])))
        selected = bands[1:]
        offset = float(np.median([b['start_s'] for b in selected]))
        good = (all(b['correlation'] >= .65 and b['envelope_correlation'] >= .8 for b in selected)
                and np.ptp([b['start_s'] for b in selected]) <= .01)
        rows.append(dict(copy=index+1, host_source_started=host_starts[index],
                         source_start_recording_s=offset, bands=bands, accepted=bool(good)))
    base = float(np.median([r['source_start_recording_s']-dt for r, dt in zip(rows, relative)]))
    for row, dt in zip(rows, relative):
        row['fit_residual_ms'] = (row['source_start_recording_s']-(base+dt))*1000
    # Two independently strong copies can establish the common clock without
    # promoting a weak third copy into a per-turn latency measurement.
    accepted = (sum(row['accepted'] for row in rows) >= 2
                and max(abs(row['fit_residual_ms']) for row in rows) <= 40
                and score[best]-competing[second] >= .1)
    return dict(accepted=bool(accepted), copies=rows, relative_playback_times_s=relative.tolist(),
                fitted_first_start_recording_s=base, wav_minus_host_monotonic_s=base-host_starts[0],
                joint_score=float(score[best]), competing_joint_score=float(competing[second]),
                competing_first_start_s=float(grid[second])-reference_offset, tolerance_ms=40,
                reference_samples=reference_samples,
                method='Three-band waveform/envelope; exact-zero padding excluded with10ms margins; original playback clock; common offset constrained by repeated waveOut intervals; high two bands drive fit')


def event(events, name):
    return next((e for e in events if e.get('stage') == name), None)


def voice_onsets(recording, lo, hi):
    # Speech-band power reduces low-frequency room rumble. Require sustained
    # activity rather than a single click; all detected rises are retained.
    sos = signal.butter(4, [300, 3500], fs=RATE, btype='bandpass', output='sos')
    energy = envelope(signal.sosfiltfilt(sos, recording))
    floor = float(np.percentile(energy, 20))
    threshold = max(64/32768, 4*floor)
    first, last = max(0, int(lo*100)), min(len(energy), int(hi*100))
    above = energy > threshold
    starts = [i*.01 for i in range(first, max(first, last-4))
              if np.count_nonzero(above[i:i+5]) >= 4 and (i == first or not above[i-1])]
    return dict(candidate_onsets_recording_s=starts, energy_threshold_fs=threshold,
                background_p20_fs=floor, grid_ms=10, sustained_rule='4 of 5 consecutive 10-ms blocks')


def progress_match(recording, center, lower, upper, recognizer, output):
    """Match the exact cached PCM; do not infer progress from firmware text."""
    candidates = []
    for language in ('mandarin', 'cantonese'):
        path = ROOT/'artifacts/voice-fast/progress-assets'/language/'c-reference.wav'
        if not path.exists():
            continue
        template = load(path)
        low = max(0, lower, center-.75)
        high = min(upper, center+.75+len(template)/RATE)
        segment = recording[round(low*RATE):round(high*RATE)]
        if len(segment) < len(template):
            continue
        bands = []
        for band in BANDS[1:]:
            sos = signal.butter(4, band, fs=RATE, btype='bandpass', output='sos')
            curve = correlate(signal.sosfiltfilt(sos, template), signal.sosfiltfilt(sos, segment))
            at = int(np.argmax(curve))
            bands.append(dict(band_hz=band, correlation=float(curve[at]), start_s=low+at/RATE))
        beginning = float(np.median([b['start_s'] for b in bands]))
        energy = envelope(template)
        active = np.flatnonzero(energy >= max(64/32768, .02*energy.max()))
        candidates.append(dict(language=language, template=str(path), template_sha256=sha(path), bands=bands,
             waveform_score=float(np.mean([b['correlation'] for b in bands])),
             start_recording_s=beginning, end_recording_s=beginning+len(template)/RATE,
             active_onset_recording_s=beginning+int(active[0])*.01,
             accepted=bool(all(b['correlation'] >= .4 for b in bands) and np.ptp([b['start_s'] for b in bands]) <= .02)))
    if not candidates:
        return dict(accepted=False, reason='No local progress reference or valid search window')
    best = max(candidates, key=lambda row: row['waveform_score'])
    result = dict(selected=best, candidates=candidates, accepted=best['accepted'],
                  classification='cached acknowledgement; excluded from final answer latency')
    if best['accepted']:
        a, b = max(lower, best['start_recording_s']-.02), min(upper, best['end_recording_s']+.06)
    else:
        # A failed waveform gate must remain failed. A wider event-bounded
        # crop can still independently identify the acknowledgement's content.
        a, b = max(lower, center-.75), min(upper, center+.75+1.3)
    pcm = recording[round(a*RATE):round(b*RATE)].astype(np.float32)
    sf.write(output, pcm, RATE, subtype='PCM_16')
    stream = recognizer.create_stream()
    stream.accept_waveform(RATE, pcm)
    recognizer.decode_stream(stream)
    result['offline_asr'] = dict(text=stream.result.text, file=str(output), sha256=sha(output), crop_s=[a,b],
                               crop_method='template matched' if best['accepted'] else 'broad event window; template match failed')
    result['energy'] = voice_onsets(recording, a, min(b, center+.75))
    return result


def dynamic_progress(recording, center, lower, upper, recognizer, output, expected):
    """Generated speech has no cached template. Keep the wider window explicit."""
    a, b = max(0, lower, center-.6), min(upper, center+8)
    result = dict(accepted=False, classification='dynamic acknowledgement; excluded from final answer latency',
                  expected_text=expected, crop_method='event-bounded energy and independent ASR, no template match')
    if b <= a:
        return dict(result, reason='No independent acknowledgement window')
    pcm = recording[round(a*RATE):round(b*RATE)].astype(np.float32)
    sf.write(output, pcm, RATE, subtype='PCM_16')
    stream = recognizer.create_stream()
    stream.accept_waveform(RATE, pcm)
    recognizer.decode_stream(stream)
    result['offline_asr'] = dict(text=stream.result.text, file=str(output), sha256=sha(output), crop_s=[a,b])
    result['energy'] = voice_onsets(recording, a, min(b, center+1.5))
    return result


def analyze(directory, recognizer, model, output_name):
    report_path = directory/'report.json'
    source_path, recording_path = directory/'prompt.wav', directory/'speaker.wav'
    report_hash = sha(report_path)
    output_path = directory/output_name
    if output_path.exists():
        raise FileExistsError(output_path)
    report = json.loads(report_path.read_text(encoding='utf-8-sig'))
    if report.get('endpoint_trace_enabled') or report.get('latency_acceptance_eligible') is False:
        raise RuntimeError('Trace-enabled capture is ineligible for response-speed analysis; use analyze_endpoint_trace.py')
    if not report.get('ended'):
        raise RuntimeError('Capture report is not terminal; no analysis performed')
    if report.get('external_recording', {}).get('sha256') != sha(recording_path):
        raise RuntimeError('Final recording hash differs from report')
    attempts = [(t, a) for t in report['trials'] for a in t.get('attempts', []) if a.get('prompt_playback')]
    if any(a.get('prompt_sha256') != sha(source_path) for _, a in attempts):
        raise RuntimeError('Repeated prompts do not share the supplied source')
    source, recording = load(source_path), load(recording_path)
    host_starts = [a['prompt_playback']['source_started'] for _, a in attempts]
    fit = align_repeated(source, recording, host_starts)
    result = dict(firmware=report.get('before', {}).get('version'), report_sha256=report_hash,
                  prompt_sha256=sha(source_path), recording_sha256=sha(recording_path),
                  script_sha256=sha(Path(__file__)), recording_seconds=len(recording)/RATE,
                  model=dict(path=str(model), sha256=sha(model/'model.int8.onnx'),
                             tokens_sha256=sha(model/'tokens.txt'), engine='local sherpa_onnx SenseVoice int8'),
                  source_alignment=fit, trials=[],
                  limitations=['Synthetic source playback, not human conversation acceptance.',
                    'No Recorder start-time assumption; common offset comes from repeated acoustic matches.',
                    'USB event receipt can be delayed/batched; it bounds search only.',
                    '10-ms energy onset is not phoneme alignment; ASR may have errors.',
                    'Progress acknowledgement is separate from the final answer; no <=1s success claim.'])
    if not fit['accepted']:
        result['status'] = 'unknown'
        result['reason'] = 'Repeated-source alignment did not meet fixed gates'
    else:
        source_energy = envelope(source)
        source_active = np.flatnonzero(source_energy >= max(64/32768, .02*source_energy.max()))
        source_end = (int(source_active[-1])+1)*.01
        # A revised analysis must not overwrite crops referenced by an older
        # report. Explicit report names get their own output directory.
        out = directory/('offline-clips' if output_name == 'offline-analysis.json'
                         else Path(output_name).stem+'-clips')
        out.mkdir(exist_ok=False)
        for index, ((trial, attempt), match) in enumerate(zip(attempts, fit['copies'])):
            events = attempt.get('events', [])
            playback, vad, asr = event(events, 'playback'), event(events, 'vad_end'), event(events, 'asr_text')
            # Independent candidates announce admission before opening their
            # speaker ring. Only a completed direct reply may use that marker;
            # task receipts are progress, not the later Agent answer. The
            # acoustic onset is still measured from the recorded waveform.
            if not playback and event(events, 'fast_reply') and not event(events, 'progress_start'):
                playback = event(events, 'candidate_play')
            progress = event(events, 'progress_start')
            progress_end = event(events, 'progress_end')
            start = match['source_start_recording_s']
            host_start = host_starts[index]
            end = start+source_end
            next_wakes = [a['wake_playback']['source_started'] for t in report['trials'] for a in t['attempts']
                          if a.get('wake_playback', {}).get('source_started', 0) > attempt['prompt_playback']['finished']]
            upper = min(len(recording)/RATE, start+min(next_wakes)-host_start-.04) if next_wakes else len(recording)/RATE
            internal = dict(playback_event=playback, speaker_start_event=event(events, 'speaker_start'),
                            first_pcm_after_vad_ms=playback['time_ms']-vad['time_ms'] if playback and vad else None,
                            first_pcm_after_final_asr_ms=playback['time_ms']-asr['time_ms'] if playback and asr else None,
                            pcm_semantics='playback event; leading zero trimming differs across firmware versions',
                            host_received_after_source_active_end_ms=(playback['observed']-host_start-source_end)*1000 if playback else None)
            row = dict(round=trial['round'], attempt=attempt['attempt'], wake_language=trial['language'],
                       input_start_recording_s=start, input_active_end_recording_s=end,
                       internal=internal, source_match_accepted=match['accepted'], progress_present=progress is not None,
                       progress_events=[e for e in events if e['stage'].startswith('progress')],
                       within_one_second=None, useful_answer_onset_s=None)
            if not playback:
                row.update(status='unknown', reason='No final playback event; progress is not an answer')
                result['trials'].append(row)
                continue
            anchor = start+playback['observed']-host_start
            low, high = max(end+.06, anchor-.75), min(upper, anchor+3)
            crop_low = end+.06
            cue_end = event(events, 'endpoint_cue_end')
            cue_unresolved = False
            if cue_end:
                # Endpoint sound is feedback, never a useful spoken answer.
                # Include the existing DMA tail plus a small acoustic margin.
                excluded = anchor+(cue_end['time_ms']-playback['time_ms'])/1000+.10
                row['endpoint_cue_exclusion_until_recording_s'] = excluded
                low, crop_low = max(low, excluded), max(crop_low, excluded)
            elif (event(events, 'candidate_play') or
                  any(e.get('stage') == 'candidate_miss' and
                      e.get('text') == 'final_local_light' for e in events)):
                # This adapter has no endpoint_cue_end telemetry. Its earlier
                # search window can include the acoustic sweep or DMA tail.
                # Reuse the fixed diagnostic sweep rule; an ambiguous/missing
                # cue leaves timing unknown instead of assuming the first
                # above-threshold sound is useful speech.
                # Local-light fallback follows the same isolated capture cue,
                # but never emits candidate_play. Its final playback receipt
                # bounds the search; it cannot promote the earlier sweep into
                # useful speech. A missing/ambiguous cue stays unknown.
                candidate_event = event(events, 'candidate_play') or playback
                candidate_anchor = start+candidate_event['observed']-host_start
                cue = find_endpoint_cue(recording, end-.3, min(upper-.18, candidate_anchor+.15))
                row['endpoint_cue_acoustic'] = cue
                cue_unresolved = not cue['present']
                if cue['present']:
                    excluded = cue['end_s']+.10
                    row['endpoint_cue_exclusion_until_recording_s'] = excluded
                    low, crop_low = max(low, excluded), max(crop_low, excluded)
            if progress:
                progress_speaker = event(events, 'progress_speaker_start')
                progress_center = start+progress['observed']-host_start
                if progress_speaker:
                    progress_center += (progress_speaker['time_ms']-progress['time_ms'])/1000
                generated = event(events, 'progress_generated')
                target = out/f'round-{trial["round"]:02}-progress.wav'
                if generated:
                    # progress_end is a join time, possibly later than audible EOF.
                    # The bounded crop is only a candidate identification window.
                    progress_upper = min(upper, anchor-.1)
                    if progress_end:
                        progress_upper = min(progress_upper, anchor+(progress_end['time_ms']-playback['time_ms'])/1000+.04)
                    matched_progress = dynamic_progress(recording, progress_center, crop_low, progress_upper,
                                                        recognizer, target, generated.get('text'))
                else:
                    matched_progress = progress_match(recording, progress_center, crop_low, min(upper, anchor-.1),
                                                      recognizer, target)
                row['progress_acoustic'] = matched_progress
                if matched_progress['accepted'] and match['accepted'] and not cue_unresolved:
                    matched_progress['ack_latency_candidate_s'] = matched_progress['selected']['active_onset_recording_s']-end
                elif match['accepted'] and not cue_unresolved:
                    rises = matched_progress.get('energy', {}).get('candidate_onsets_recording_s', [])
                    if rises and matched_progress.get('offline_asr', {}).get('text'):
                        matched_progress['energy_ack_latency_candidate_s'] = rises[0]-end
                if not progress_end:
                    row.update(status='unknown', reason='Cannot separate unbounded progress audio')
                    result['trials'].append(row)
                    continue
                # Use final-playback receipt plus the board-clock difference;
                # retrospective progress_end receipt itself may arrive late.
                excluded_until = (matched_progress['selected']['end_recording_s']+.06 if matched_progress['accepted'] else
                    anchor+(progress_end['time_ms']-playback['time_ms'])/1000+.04)
                low = max(low, excluded_until)
                crop_low = max(crop_low, excluded_until)
                row['progress_exclusion_until_recording_s'] = excluded_until
            stage_start, stage_end = event(events, 'stage_start'), event(events, 'stage_end')
            if stage_start:
                row['stage_notice'] = dict(text=stage_start.get('text'), end=stage_end)
                if not stage_end:
                    row.update(status='unknown', reason='Cannot separate unbounded stage notice')
                    result['trials'].append(row)
                    continue
                excluded = anchor+(stage_end['time_ms']-playback['time_ms'])/1000+.06
                low, crop_low = max(low, excluded), max(crop_low, excluded)
                row['stage_notice']['exclusion_until_recording_s'] = excluded
            row['event_search_window_s'] = [low, high]
            onsets = voice_onsets(recording, low, high)
            row['acoustic'] = onsets
            onsets['latencies_from_input_end_s'] = [v-end for v in onsets['candidate_onsets_recording_s']]
            if upper <= crop_low:
                row.update(status='unknown', reason='No independent final-reply crop')
                result['trials'].append(row)
                continue
            reply = recording[round(crop_low*RATE):round(upper*RATE)].astype(np.float32)
            target = out/f'round-{trial["round"]:02}-reply.wav'
            sf.write(target, reply, RATE, subtype='PCM_16')
            stream = recognizer.create_stream()
            stream.accept_waveform(RATE, reply)
            recognizer.decode_stream(stream)
            text = stream.result.text.strip()
            compact = ''.join(c for c in text if c.isalnum())
            acknowledgement_only = compact in ('收到', '收到我来处理', '收到等我处理')
            row['offline_asr'] = dict(text=text, crop_start_recording_s=crop_low,
                crop_end_recording_s=upper, file=str(target), sha256=sha(target),
                nonempty=bool(compact), acknowledgement_only=acknowledgement_only,
                full_scale_samples=int(np.count_nonzero(abs(reply) >= 32767/32768)),
                peak_fs=float(np.max(abs(reply))), rms_fs=float(np.sqrt(np.mean(reply**2))))
            candidate = onsets['candidate_onsets_recording_s']
            if cue_unresolved:
                row.update(status='unknown', reason='Cannot separate endpoint feedback from candidate speech')
            else:
                row.update(answer_candidate(attempt, match['accepted'], candidate, text, end))
            result['trials'].append(row)
            print(json.dumps(dict(directory=directory.name, round=trial['round'], text=text,
                  candidate_latency_s=row.get('acoustic_answer_latency_candidate_s'),
                  internal_after_vad_ms=internal['first_pcm_after_vad_ms']), ensure_ascii=False), flush=True)
        result['status'] = 'analyzed'
    if sha(report_path) != report_hash:
        raise RuntimeError('Original report changed during analysis; do not publish')
    output_path.write_text(json.dumps(result, ensure_ascii=False, indent=2)+'\n', encoding='utf8')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directories', type=Path, nargs='+')
    parser.add_argument('--model', type=Path, default=ROOT/'artifacts/kws-phase2/asr')
    parser.add_argument('--output-name', default='offline-analysis.json')
    args = parser.parse_args()
    if Path(args.output_name).name != args.output_name:
        parser.error('--output-name must be a filename within each input directory')
    import sherpa_onnx
    recognizer = sherpa_onnx.OfflineRecognizer.from_sense_voice(
        model=str(args.model/'model.int8.onnx'), tokens=str(args.model/'tokens.txt'),
        num_threads=4, use_itn=True, language='auto')
    for directory in args.directories:
        analyze(directory, recognizer, args.model, args.output_name)


if __name__ == '__main__':
    main()
