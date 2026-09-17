"""Read-only source/build audit; outputs counts and findings, never secret values."""
import argparse
import json
from pathlib import Path
import re
import subprocess

root = Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--build',type=Path,default=Path('build-agent'),help='Build whose actual compiler modes are audited')
p.add_argument('--output',type=Path,default=Path('artifacts/logs/source-audit.json'))
args = p.parse_args()
paths = subprocess.check_output(["git", "ls-files", "--cached", "--others", "--exclude-standard"], cwd=root, text=True).splitlines()
owned = [p for p in paths if not p.startswith("third_party/") and p != "deep-research-report.md"]
findings = []
for name in owned:
    path = root / name
    if path.suffix not in (".c", ".h", ".py", ".ps1", ".md", ".json", ".yml", ".txt"):
        continue
    text = path.read_text(encoding="utf-8-sig")
    if re.search(r"sk-[A-Za-z0-9]{20,}", text):
        findings.append({"path": name, "kind": "possible embedded API key"})
    if name.startswith(("core/", "plugins/")) and re.search(r'#include\s*[<"](?:esp_|freertos/|driver/)', text):
        findings.append({"path": name, "kind": "platform include leaked into portable code"})
for name in paths:
    if name.startswith((".local/", ".toolchains/", "_ref/", "artifacts/", "build/", "build-host/", "managed_components/")):
        findings.append({"path": name, "kind": "generated/private file is not ignored"})
commands = json.loads((root / args.build / 'compile_commands.json').read_text(encoding="utf-8"))
owned_commands = []
for entry in commands:
    file = Path(entry["file"])
    try:
        relative = file.relative_to(root).as_posix()
    except ValueError:
        continue
    if relative.startswith(("core/", "plugins/", "platform/espidf/", "boards/", "main/", "components/agent_speech/", "components/agent_vad/")):
        standards = re.findall(r"-std=([^\s]+)", entry["command"])
        owned_commands.append({"file": relative, "effective_standard": standards[-1] if standards else None})
        if not standards or standards[-1] != "c11":
            findings.append({"path": relative, "kind": "effective compiler standard is not C11"})
for name in ["README.md", "docs/ARCHITECTURE.md", "docs/HARDWARE_FACTS.md", "docs/PORTING.md", "docs/CONTEXT_PROTOCOL.md",
             "docs/GATEWAY_PROTOCOL.md", "docs/SECURITY.md", "docs/TEST_PLAN.md", "docs/BUILD_REPORT.md", "docs/TEST_REPORT.md", "SPEC.md", "ACTIONLOG.md"]:
    if not (root/name).is_file():
        findings.append({"path": name, "kind": "missing required document"})
for path in (root / "protocol").glob("*.json"):
    json.loads(path.read_text(encoding="utf-8"))
report = {"build":str(args.build), "source_files": len(owned), "owned_firmware_c_files": len(owned_commands),
          "c_lines": sum(len((root/p).read_text(encoding="utf-8").splitlines()) for p in owned if p.endswith(".c") and not p.startswith("host_tests/")),
          "compile_modes": owned_commands, "findings": findings, "passed": not findings}
(root/args.output).write_text(json.dumps(report, indent=2), encoding="utf-8")
print(json.dumps({k:v for k,v in report.items() if k != "compile_modes"}, indent=2))
raise SystemExit(0 if report["passed"] else 1)
