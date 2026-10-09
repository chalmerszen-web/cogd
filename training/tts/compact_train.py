"""Bounded local compact speech from already registered TRAIN identities.

No held-out reference is loaded. This is source preparation, not KWS training,
human pronunciation validation or a claim of independent test performance.
"""
import gc
import hashlib
import json
import os
from pathlib import Path
import random
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
MODEL_REVISIONS = dict(zh="eec1ae6c79877dbd9379285cf8789c9e0879293d",
                       yue="b6d0eb0b4b594c67100e8786fe97227042d451a9")
TARGETS = dict(zh="你好小言", yue="你好小賢")
YUE_REFERENCE_TEXT = "今朝天氣好好，我哋一齊出去行下，睇吓路邊啲小花。"


def requests(rows, voices, speeds=(1., 1.25), seed=2026100373):
    """Require every descendant of each reference identity to remain TRAIN."""
    if not voices or len(voices) != len(set(voices)) or len(voices) > 8:
        raise ValueError("one to eight unique TRAIN identities required")
    if not speeds or len(speeds) != len(set(speeds)) or any(not .8 <= speed <= 1.4 for speed in speeds):
        raise ValueError("unique speeds within the established TTS budget required")
    if len(voices)*len(speeds)*2 > 32:
        raise ValueError("single pilot capped at32 sources")
    if not isinstance(seed, int) or not 0 <= seed <= 2**32-1000000:
        raise ValueError("seed outside uint32 budget")
    for voice in voices:
        if not voice.replace("_", "").isalnum():
            raise ValueError("invalid reference identity")
        group = "kokoro-v1-"+voice
        parents = [row for row in rows if row["source_group"] == group]
        if not parents or any(row["split"] != "train" for row in parents):
            raise ValueError("reference is not exclusively TRAIN: "+voice)
        if {row["language"] for row in parents if row["label"]} != {"zh", "yue"}:
            raise ValueError("both TRAIN languages required: "+voice)
    return [dict(clip_id=f"compact-train-{language}-{voice}-s{case}",
        language=language, voice=voice, source_group="kokoro-v1-"+voice,
        split="train", label=1, text="你好，小言", source_text=TARGETS[language],
        synthesis_speed=float(speed), seed=seed+index*10000+case*17+li*500000)
        for li, language in enumerate(("zh", "yue"))
        for index, voice in enumerate(voices) for case, speed in enumerate(speeds)]


def generate(config, out):
    """Generate one frozen request list locally; never retry a failed utterance."""
    out = Path(out)
    if out.exists():
        raise ValueError("pilot output already exists")
    out.mkdir(parents=True)
    for key in ("HF_HUB_OFFLINE", "TRANSFORMERS_OFFLINE", "HF_HUB_DISABLE_TELEMETRY", "COGD_TTS_PRENORMALIZED"):
        os.environ[key] = "1"
    import numpy as np
    import soundfile as sf
    import torch
    sys.path[:0] = [str(ROOT/"training/tts"), str(ROOT/"training/kws")]
    from generate import save_wave
    from boundary import interval
    source = ROOT/config["source"]
    sys.path[:0] = [str(source), str(source/"third_party/Matcha-TTS")]
    from cosyvoice.cli.cosyvoice import CosyVoice2
    rows = [json.loads(line) for line in (ROOT/config["manifest"]).read_text(encoding="utf8").splitlines() if line.strip()]
    expected = requests(rows, config["voices"], config["speeds"], config["seed"])
    if expected != config["requests"]:
        raise ValueError("frozen requests no longer match TRAIN source membership")
    torch.set_num_threads(4)
    if not torch.cuda.is_available():
        raise RuntimeError("frozen CUDA pilot requires the already verified GPU")
    began = time.monotonic()
    manifest = out/"manifest.jsonl"
    generated = []
    torch.cuda.reset_peak_memory_stats()
    for language in ("zh", "yue"):
        if time.monotonic()-began > config["maximum_seconds"]:
            raise TimeoutError("finite32-source synthesis; no restart")
        modeldir = ROOT/config["models"]/MODEL_REVISIONS_DIRECTORY[language]
        model = CosyVoice2(str(modeldir), load_jit=False, load_trt=False,
                           load_vllm=False, fp16=True)
        for request in [row for row in expected if row["language"] == language]:
            if time.monotonic()-began > config["maximum_seconds"]:
                raise TimeoutError("finite32-source synthesis; no restart")
            prefix = "reference-yue-" if language == "yue" else "reference-"
            reference_path = ROOT/config["references"]/(prefix+request["voice"]+".wav")
            reference, rate = sf.read(reference_path, dtype="float32")
            assert rate == 16000 and reference.ndim == 1
            prompt = torch.from_numpy(reference)[None]
            torch.manual_seed(request["seed"])
            random.seed(request["seed"])
            np.random.seed(request["seed"])
            start = time.monotonic()
            if language == "yue":
                chunks = list(model.inference_zero_shot(request["source_text"],
                    YUE_REFERENCE_TEXT, prompt, stream=False, text_frontend=False,
                    speed=request["synthesis_speed"]))
            else:
                chunks = list(model.inference_instruct2(request["source_text"],
                    "用普通话说这句话", prompt, stream=False, text_frontend=False,
                    speed=request["synthesis_speed"]))
            audio = torch.cat([chunk["tts_speech"].cpu() for chunk in chunks], dim=1)[0].numpy()
            path = out/(request["clip_id"]+".wav")
            pcm = save_wave(path, audio, model.sample_rate)
            try:
                bounds = interval(pcm)
                bounds_error = None
            except ValueError as error:
                bounds, bounds_error = {}, str(error)
            row = dict(request, path=path.relative_to(ROOT).as_posix(),
                reference_speaker_id=request["source_group"], original_recording_id=request["clip_id"],
                reference_path=reference_path.relative_to(ROOT).as_posix(),
                reference_file_sha256=hashlib.sha256(reference_path.read_bytes()).hexdigest(),
                reference_pcm_sha256=hashlib.sha256(reference.tobytes()).hexdigest(),
                pcm_sha256=hashlib.sha256(pcm.tobytes()).hexdigest(), license_id="Apache-2.0-models",
                carrier=False, compact_positive=True, needs_alignment=bool(bounds_error),
                endpoint_schema=1, annotation="energy interval, automatic screening only; no phonetic truth",
                wake_start_sample=bounds.get("speech_start_sample", 0),
                wake_end_sample=bounds.get("speech_end_high_sample", len(pcm)),
                bounds_error=bounds_error, seconds=len(pcm)/16000,
                generation_seconds=time.monotonic()-start, tts_family="CosyVoice2",
                tts_model_revision=MODEL_REVISIONS[language],
                reference_model_revision="f3ff3571791e39611d31c381e3a41a3af07b4987", **bounds)
            with manifest.open("a", encoding="utf8") as stream:
                stream.write(json.dumps(row, ensure_ascii=False)+"\n")
            generated.append(row)
            print(json.dumps(dict(generated=len(generated), total=len(expected), language=language,
                seconds=row["seconds"], generation_seconds=row["generation_seconds"],
                bounds_error=bounds_error)), flush=True)
        del model, chunks, prompt
        gc.collect()
        torch.cuda.empty_cache()
    if time.monotonic()-began > config["maximum_seconds"]:
        raise TimeoutError("finite32-source synthesis; no restart")
    report = dict(complete=True, generated=len(generated), maximum=len(expected),
        seconds=time.monotonic()-began, GPU_name=torch.cuda.get_device_name(0),
        GPU_peak_allocated_bytes=torch.cuda.max_memory_allocated(),
        seed=config["seed"], voices=config["voices"], speeds=config["speeds"],
        revisions=MODEL_REVISIONS, manifest_sha256=hashlib.sha256(manifest.read_bytes()).hexdigest(),
        no_private_reference_or_cloud=True, all_sources_TRAIN=True,
        no_KWS_quality_claim=True, no_fit=True, automatic_screening_required=True)
    (out/"report.json").write_text(json.dumps(report, ensure_ascii=False, indent=2)+"\n", encoding="utf8")
    return report


MODEL_REVISIONS_DIRECTORY = dict(zh="mandarin", yue="cantonese")
