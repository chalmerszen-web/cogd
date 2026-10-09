"""Fixed local screening of a compact TRAIN-only speech pilot.

ASR provides a conservative screening flag, never pronunciation ground truth.
Keep every failed input; do not retry synthesis or alter an existing split.
"""
import hashlib
import json
from pathlib import Path
import re
import sys
import time

ROOT = Path(__file__).resolve().parents[2]


def reason(text, detected_language, language, bounds_error=None):
    from curate import compact, target_positions
    if bounds_error:
        return "energy_interval_uncertain"
    if re.search(r"[A-Za-z]", text):
        return "mixed_script_transcript"
    if len(compact(text)) != 4 or target_positions(text, language) != [0]:
        return "complete_four_syllables_uncertain"
    if detected_language != "<|"+language+"|>":
        return "language_uncertain"
    return None


def screen(config, source, out):
    import numpy as np
    import soundfile as sf
    import sherpa_onnx
    sys.path[:0] = [str(ROOT/"training/tts"), str(ROOT/"training/kws")]
    from boundary import interval
    from curate import target_positions
    from energy_cut import incomplete
    from compact_train import requests
    out, source = Path(out), Path(source)
    out.mkdir(parents=True, exist_ok=False)
    rows = [json.loads(line) for line in (ROOT/config["manifest"]).read_text(encoding="utf8").splitlines() if line.strip()]
    expected = requests(rows, config["voices"], config["speeds"], config["seed"])
    sources = [json.loads(line) for line in (source/"manifest.jsonl").read_text(encoding="utf8").splitlines() if line.strip()]
    assert len(sources) == len(expected) == 32
    for actual, request in zip(sources, expected):
        assert all(actual[key] == value for key, value in request.items())
    began = time.monotonic()
    base = ROOT/config["ASR"]
    decoder = sherpa_onnx.OfflineRecognizer.from_sense_voice(
        model=str(base/"model.int8.onnx"), tokens=str(base/"tokens.txt"),
        num_threads=4, use_itn=True, language="auto")
    audit, accepted = [], []
    decodes = 0

    def decode(pcm):
        nonlocal decodes
        if time.monotonic()-began > config["screen_maximum_seconds"]:
            raise TimeoutError("finite compact screen; no decoder search")
        stream = decoder.create_stream()
        signal = np.r_[pcm.astype(np.float32)/32768, np.zeros(8000, np.float32)]
        stream.accept_waveform(16000, signal)
        decoder.decode_stream(stream)
        decodes += 1
        return dict(text=stream.result.text, detected_language=stream.result.lang)

    for row in sources:
        pcm, rate = sf.read(ROOT/row["path"], dtype="int16")
        assert rate == 16000 and pcm.ndim == 1
        assert hashlib.sha256(pcm.astype("<i2").tobytes()).hexdigest() == row["pcm_sha256"]
        bounds_error = row["bounds_error"]
        try:
            bounds = interval(pcm)
            assert bounds_error is None
            assert all(bounds[key] == row[key] for key in bounds)
            span = bounds["speech_end_high_sample"]-bounds["speech_start_sample"]
            if span < 6400:
                bounds_error = "speech_under_400ms"
        except ValueError as error:
            bounds, bounds_error, span = {}, str(error), None
        result = decode(pcm)
        rejected = reason(result["text"], result["detected_language"], row["language"], bounds_error)
        halves = []
        for direction in ("prefix", "suffix"):
            try:
                fragment, trace = incomplete(pcm, 0, len(pcm), direction, .5)
                half = dict(direction=direction, **trace.manifest(), **decode(fragment))
                half["full_target_flag"] = bool(target_positions(half["text"], row["language"]))
                if half["full_target_flag"] and rejected is None:
                    rejected = "half_utterance_has_complete_target_flag"
            except ValueError as error:
                half = dict(direction=direction, error=str(error), full_target_flag=None)
                if rejected is None:
                    rejected = "energy_cut_contract_uncertain"
            halves.append(half)
        note = dict(clip_id=row["clip_id"], language=row["language"],
            voice=row["voice"], synthesis_speed=row["synthesis_speed"],
            pcm_sha256=row["pcm_sha256"], **result, bounds_error=bounds_error,
            speech_span_ms=span/16 if span is not None else None,
            halves=halves, rejected_reason=rejected, split="train")
        audit.append(note)
        if rejected is None:
            accepted.append(dict(row, annotation="fixed automatic ASR/energy screening; no human pronunciation proof"))
        with (out/"audit.jsonl").open("a", encoding="utf8") as stream:
            stream.write(json.dumps(note, ensure_ascii=False)+"\n")
        print(json.dumps(dict(screened=len(audit), total=32, decodes=decodes,
            language=row["language"], accepted=rejected is None, reason=rejected)), flush=True)
    groups = {}
    for language in ("zh", "yue"):
        selected = [row for row in accepted if row["language"] == language]
        speeds = {str(speed): sum(row["synthesis_speed"] == speed for row in selected)
                  for speed in config["speeds"]}
        voices = sorted({row["voice"] for row in selected})
        groups[language] = dict(accepted=len(selected), voices=voices, speeds=speeds,
            sufficient=len(selected) >= 8 and len(voices) >= 4 and min(speeds.values()) >= 3)
    (out/"manifest.jsonl").write_text("".join(json.dumps(row, ensure_ascii=False)+"\n" for row in accepted), encoding="utf8")
    report = dict(complete=True, screened=32, decodes=decodes, maximum_decodes=96,
        accepted=len(accepted), rejected=32-len(accepted), groups=groups,
        sufficient_for_next_bounded_feature_plan=all(group["sufficient"] for group in groups.values()),
        seconds=time.monotonic()-began, no_KWS_quality_claim=True, no_test_or_DEV_waveforms=True,
        no_new_capture_playback_cloud_USB_Flash=True, no_fit=True,
        failed_sources_kept=True, split="train", no_human_generalization_claim=True)
    assert decodes <= 96 and report["seconds"] <= config["screen_maximum_seconds"]
    (out/"report.json").write_text(json.dumps(report, ensure_ascii=False, indent=2)+"\n", encoding="utf8")
    return report
