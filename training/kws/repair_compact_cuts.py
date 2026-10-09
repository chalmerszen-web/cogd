"""Replay compact TRAIN augmentation to repair acoustically uncut negatives.

This creates a derivative archive. Old archives, labels, positive endpoints and
random draws remain unchanged. Cached frozen-model features for changed rows
must be regenerated before using the derivative in any model fit/evaluation.
"""
import copy
import hashlib
import json
from pathlib import Path

import numpy as np

from data import ROOT, Frontend, framed_example, load_pcm, normalize
from append_captures import capture_path
from device_domain import pcm
from paired_negative import incomplete as time_cut
from energy_cut import incomplete as energy_cut


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def prepare(files, out):
    """Apply fixed >90% retained-reference screen only to compact TRAIN cuts."""
    out = Path(out)
    if out.exists():
        raise ValueError("output already exists; never replace a previous run")
    out.mkdir(parents=True)
    paths = {key: ROOT / value for key, value in files.items()}
    inputs = {str(path.relative_to(ROOT)): sha(path) for path in paths.values()}
    with np.load(paths["train"], allow_pickle=False) as archive:
        original = {key: archive[key] for key in archive.files}
    meta = json.loads(paths["compact_metadata"].read_text(encoding="utf8"))
    raw_rows = {row["clip_id"]: row for row in
                (json.loads(line) for line in paths["compact_manifest"].read_text(encoding="utf8").splitlines() if line.strip())}
    stats = json.loads(paths["normalization"].read_text(encoding="utf8"))
    captures = json.loads(paths["room_metadata"].read_text(encoding="utf8"))["captures"]
    noise = []
    for capture in captures:
        if capture["split"] == "train":
            path = capture_path(capture["recording"])
            inputs[str(path.relative_to(ROOT))] = sha(path)
            noise.append(pcm(path)[-16000:])
    if len(noise) < 20:
        raise ValueError("old augmentation room floor is incomplete")
    frontend = Frontend(paths["frontend"])
    rng = np.random.default_rng(202609221)
    changes, reconstructed_values, replayed = [], 0, 0
    updated = original["x"].copy()
    for source in meta["sources"]:
        if source["split"] != "train":
            continue  # no DEV/TEST waveform is read or processed
        row = raw_rows[source["clip_id"].removeprefix("compact-")]
        assert row["split"] == "train" and row["label"] == 1
        assert row["pcm_sha256"] == source["pcm_sha256"]
        path = capture_path(row["path"])
        inputs[str(path.relative_to(ROOT))] = sha(path)
        raw = load_pcm(row)
        bounds = {key: source[key] for key in
                  ("speech_start_sample", "speech_end_low_sample", "speech_end_high_sample")}
        moved = dict(row, **bounds, endpoint_schema=1,
                     clip_id=source["clip_id"], sampling_group="positive")
        variants = [(moved, raw, None, None)]
        for side in ("prefix", "suffix"):
            old, old_cut = time_cut(raw, bounds["speech_start_sample"],
                                    bounds["speech_end_low_sample"], side)
            a, b = bounds["speech_start_sample"], bounds["speech_end_low_sample"]
            retained = float(np.sum(old[a:b].astype(np.float64)**2) /
                             np.sum(raw[a:b].astype(np.float64)**2))
            # Near-silent negatives retain their original valid negative label
            # and input. Replace only a cut that still keeps >90% speech energy.
            new, trace = (energy_cut(raw, a, b, side) if retained > .90 else (None, None))
            negative = dict(moved, clip_id=moved["clip_id"]+"-"+side, label=0,
                            text="time-cut-"+side, sampling_group="paired_negative")
            variants.append((negative, old, new,
                             dict(old_cut=old_cut, old_retained_reference_energy_fraction=retained,
                                  new=trace.manifest()) if trace else None))
        for variant_row, signal, replacement, audit in variants:
            for variant in range(9):
                indices = np.flatnonzero((original["clip_id"] == variant_row["clip_id"]) &
                                         (original["variant"] == variant))
                assert len(indices) == 1
                index = int(indices[0])
                background = noise[int(rng.integers(len(noise)))] if variant else None
                before = copy.deepcopy(rng.bit_generator.state)
                old_wave, labels, end = framed_example(variant_row, signal, rng if variant else None,
                                                       background=background)
                np.testing.assert_array_equal(labels, original["y"][index])
                assert original["label"][index] == variant_row["label"]
                assert original["event_end"][index] == (-1 if end is None else end)
                replayed += 1
                if replacement is None:
                    continue
                assert original["label"][index] == 0
                old_features = normalize(frontend(old_wave), stats)
                np.testing.assert_array_equal(old_features, original["x"][index])
                reconstructed_values += old_features.size
                new_rng = np.random.default_rng()
                new_rng.bit_generator.state = before
                wave, new_labels, new_end = framed_example(variant_row, replacement,
                    new_rng if variant else None, background=background)
                assert new_rng.bit_generator.state == rng.bit_generator.state
                np.testing.assert_array_equal(new_labels, labels)
                assert new_end is None and end is None
                updated[index] = normalize(frontend(wave), stats)
                assert not np.array_equal(updated[index], old_features)
                changes.append(dict(index=index, clip_id=variant_row["clip_id"], variant=variant,
                    augmented_PCM_sha256=hashlib.sha256(wave.astype("<i2").tobytes()).hexdigest(), **audit))
    selected = np.array([row["index"] for row in changes], dtype=np.int64)
    assert len(selected) and len(set(selected)) == len(selected)
    untouched = np.ones(len(updated), dtype=bool)
    untouched[selected] = False
    np.testing.assert_array_equal(updated[untouched], original["x"][untouched])
    np.testing.assert_array_equal(updated[original["label"] == 1], original["x"][original["label"] == 1])
    np.savez_compressed(out/"delta.npz", index=selected, old_x=original["x"][selected], new_x=updated[selected])
    np.savez_compressed(out/"train.npz", **dict(original, x=updated))
    # Reopen what was actually saved, including unchanged metadata/labels.
    with np.load(out/"train.npz", allow_pickle=False) as saved:
        for key, value in original.items():
            np.testing.assert_array_equal(saved[key], updated if key == "x" else value)
    for path, digest in inputs.items():
        assert sha(ROOT/path) == digest, path
    result = dict(complete=True, source_inputs=inputs, sources_replayed=sum(r["split"]=="train" for r in meta["sources"]),
        augmentation_seed=202609221, augmentation_rows_replayed=replayed, changes=changes,
        changed_negative_rows=len(selected), old_C_frontend_values_exact=reconstructed_values,
        all_positive_inputs_and_all_labels_and_metadata_unchanged=True,
        all_other_negative_inputs_unchanged=True, original_files_unchanged=True,
        original_train_sha256=sha(paths["train"]), derivative_train_sha256=sha(out/"train.npz"),
        delta_sha256=sha(out/"delta.npz"), cached_frozen_features_must_be_rebuilt=True,
        no_phonetic_truth_or_KWS_quality_claim=True, no_DEV_or_TEST_audio=True, no_fit=True)
    (out/"repair.json").write_text(json.dumps(result, ensure_ascii=False, indent=2)+"\n", encoding="utf8")
    return result
