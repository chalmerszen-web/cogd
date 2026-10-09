"""Summarize recorded endpoint evidence; never opens USB/audio or calls a service."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
from statistics import mean, median


def analyze(report: dict) -> dict:
    rows = []
    for trial in report.get("trials", []):
        wake = trial.get("wake", {})
        verify = wake.get("verify", {})
        frames = verify.get("frames", 0)
        events = trial.get("events", [])
        final = next((e for e in events if e.get("stage") == "asr_final"), None)
        pcm = next((e for e in events if e.get("stage") == "playback"), None)
        source_end = trial.get("utterance_playback", {}).get("finished")
        capture_end = wake.get("end_at")
        row = {
            "id": trial.get("id"), "complete": trial.get("complete", False),
            "language": trial.get("wake_language"),
            "capture_wall_ms": verify.get("capture_us", 0) / 1000,
            "source_ms": verify.get("source_ms"),
            "confirm_source_ms": verify.get("confirm_ms"),
            "confirm_wall_ms": verify.get("confirm_wall_ms"),
            "confirm_lag_ms": verify.get("confirm_wall_ms", 0) - verify.get("confirm_ms", 0),
            "max_backlog_ms": verify.get("backlog_samples", 0) / 16,
            "neural_frames": frames,
            "neural_cpu_ms_per_16ms_frame": verify.get("nn_cpu_us", 0) / (1000 * frames) if frames else None,
            "neural_wall_ms_per_16ms_frame": verify.get("nn_wall_us", 0) / (1000 * frames) if frames else None,
            "producer_cpu_ms": verify.get("producer_cpu_us", 0) / 1000,
            "speech_classified_ms": wake.get("speech_ms"),
            "end_silence_ms": verify.get("end_silence_ms"),
            "quiet_ms_at_end": verify.get("quiet_ms"),
            "capture_end_minus_asr_final_ms": capture_end - final["time_ms"] if capture_end and final else None,
            "input_end_to_capture_end_est_ms": (
                (pcm["observed"] - source_end) * 1000 - (pcm["time_ms"] - capture_end)
                if capture_end and pcm and source_end else None
            ),
            "dma_lost": wake.get("dma_lost"), "error": trial.get("error"),
        }
        rows.append(row)
    summary = {}
    for key in ("confirm_lag_ms", "max_backlog_ms", "neural_cpu_ms_per_16ms_frame",
                "neural_wall_ms_per_16ms_frame", "input_end_to_capture_end_est_ms"):
        values = [r[key] for r in rows if r["complete"] and r[key] is not None]
        if values:
            summary[key] = {"mean": mean(values), "median": median(values),
                            "min": min(values), "max": max(values)}
    return {"schema": "endpoint-analysis-v1", "trials": rows, "summary": summary,
            "notes": ["16 kHz PCM; TEN frame is 256 samples / 16 ms.",
                      "The nn counters wrap esp_hi_vad_process: TEN frontend and inference together, not only neural layers.",
                      "Wall inference includes preemption; CPU counters are device task measurements.",
                      "Input-end estimate includes USB receipt delay and PC playback scheduling.",
                      "A near-zero capture-minus-final suggests ASR-assisted termination, not causation proof.",
                      "No end timer or speech probabilities can be reconstructed from aggregate counters."]}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("report", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    result = analyze(json.loads(args.report.read_text(encoding="utf-8-sig")))
    result["input"] = str(args.report)
    text = json.dumps(result, ensure_ascii=False, indent=2)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(text + "\n", encoding="utf-8")
    print(text)


if __name__ == "__main__":
    main()
