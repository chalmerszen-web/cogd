"""Host benchmarks and C3 object sizes only; does not access or flash hardware."""
from __future__ import annotations

import ctypes
import argparse
import json
from pathlib import Path
import re
import statistics
import subprocess

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "artifacts/voice-flow/producer-study"


def run(args: list[str], cwd=ROOT) -> str:
    process = subprocess.run(args, cwd=cwd, text=True, encoding="utf-8", errors="replace", capture_output=True)
    if process.returncode:
        raise RuntimeError(process.stdout + process.stderr)
    return process.stdout


def windows_args(command: str) -> list[str]:
    shell = ctypes.windll.shell32
    shell.CommandLineToArgvW.argtypes = [ctypes.c_wchar_p, ctypes.POINTER(ctypes.c_int)]
    shell.CommandLineToArgvW.restype = ctypes.POINTER(ctypes.c_wchar_p)
    count = ctypes.c_int()
    pointer = shell.CommandLineToArgvW(command, ctypes.byref(count))
    try:
        return [pointer[i] for i in range(count.value)]
    finally:
        ctypes.windll.kernel32.LocalFree(pointer)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", default="build-kws-fusion-ek")
    args = parser.parse_args()
    OUT.mkdir(parents=True, exist_ok=True)
    records = []
    database = ROOT / args.build_dir / "compile_commands.json"
    compiles = json.loads(database.read_text())
    chosen = {"voice.c", "tonal.c", "source_stream.c", "source_bound.c", "clip_packed.c", "audio_board.c"}
    for item in compiles:
        source = Path(item["file"])
        if source.name not in chosen:
            continue
        base = windows_args(item["command"])
        base = [a for a in base if not re.fullmatch(r"-O(?:s|[0-3]|fast|g)", a)]
        for flag in ("Os", "O3"):
            obj = OUT / f"{source.stem}-{flag}.o"
            args = base.copy()
            args[args.index("-o") + 1] = str(obj)
            args.extend([f"-{flag}", "-fstack-usage"])
            run(args, item["directory"])
            tool = Path(args[0])
            size = run([str(tool.with_name("riscv32-esp-elf-size.exe")), str(obj)])
            text, data, bss, *_ = size.strip().splitlines()[-1].split()
            assembly = run([str(tool.with_name("riscv32-esp-elf-objdump.exe")), "-dr", str(obj)])
            (OUT / f"{source.stem}-{flag}.asm").write_text(assembly)
            records.append({"file": str(source.relative_to(ROOT)), "flag": flag,
                            "text": int(text), "data": int(data), "bss": int(bss),
                            "division_helpers": sorted(set(re.findall(r"__(?:u?div|u?mod)[a-z0-9]+", assembly)))})

    # Fixed source set; no CMake state or current firmware is modified.
    source = "tools/voice/benchmark_producer.c plugins/audio/voice.c plugins/audio/tonal.c plugins/audio/source_stream.c plugins/audio/source_bound.c"
    base = "gcc -std=c11 -Wall -Wextra -Werror -Icore -Iplugins/audio -DAGENT_PACKED_CLIP=1 -ffunction-sections -fdata-sections"
    commands = [f"{base} -{flag} {source} -Wl,--gc-sections -o /tmp/cogd-producer-{flag}" for flag in ("Os", "O3")]
    run(["wsl", "-d", "Ubuntu", "--", "sh", "-lc", " && ".join(commands)])
    host = []
    for repeat in range(3):
        for mode in ("b", "s"):
            for flag in (("Os", "O3") if repeat % 2 == 0 else ("O3", "Os")):
                result = run(["wsl", "-d", "Ubuntu", "--", f"/tmp/cogd-producer-{flag}", mode, "8"])
                value = json.loads(result)
                value.update({"flag": flag, "run": repeat + 1})
                host.append(value)
    summary = []
    for mode in ("seven_biquads", "source_metadata"):
        selected = [r for r in host if r["mode"] == mode]
        assert len({(r["digest"], r["samples"]) for r in selected}) == 1
        times = {flag: statistics.median(r["ns_per_sample"] for r in selected if r["flag"] == flag) for flag in ("Os", "O3")}
        summary.append({"mode": mode, "identical_digest": selected[0]["digest"], "samples_per_run": selected[0]["samples"],
                        "median_ns_per_sample": times, "host_only_speedup": times["Os"] / times["O3"]})
    report = {"compile_database": str(database), "c3_objects": records, "host": host, "summary": summary,
              "limits": ["Object .text includes functions later removed by linker GC; final image size must be rebuilt.",
                         "Host timings are x86 measurements, not C3 speed claims.",
                         "source_metadata uses a deterministic spectral substitute, excluding WebRTC cost.",
                         "No thresholds, algorithms, input samples or arithmetic ordering were changed."]}
    (OUT / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({"c3_objects": records, "summary": summary}, indent=2))


if __name__ == "__main__":
    main()
