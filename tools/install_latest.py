"""Validate and install the retained application; never change partition/data regions."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys

from migrate_context import application_size, partitions

ROOT = Path(__file__).resolve().parents[1]
PACKAGE = ROOT / "firmware" / "latest"


def check_package():
    manifest = json.loads((PACKAGE / "manifest.json").read_text(encoding="utf-8"))
    for name in ("esp_hi_agent.bin", "partition-table.bin"):
        data = (PACKAGE / name).read_bytes()
        expected = manifest["files"][name]
        if len(data) != expected["bytes"] or hashlib.sha256(data).hexdigest() != expected["sha256"]:
            raise ValueError(f"Package checksum mismatch: {name}")
    app = (PACKAGE / "esp_hi_agent.bin").read_bytes()
    table = partitions((PACKAGE / "partition-table.bin").read_bytes())
    if (table["factory"]["offset"], table["factory"]["size"]) != (0x10000, 0x180000):
        raise ValueError("Unsupported application layout")
    if application_size(app) != len(app) or len(app) > table["factory"]["size"]:
        raise ValueError("Invalid or oversized application")
    return manifest, table


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="COM5")
    parser.add_argument("--check", action="store_true", help="Check local files without opening USB")
    args = parser.parse_args()
    manifest, expected_table = check_package()
    print(f"Package verified: {manifest['firmware']} ({manifest['files']['esp_hi_agent.bin']['bytes']} bytes)")
    if args.check:
        return
    subprocess.run([sys.executable, str(ROOT / 'tools/kws/flash_guard.py'),
                    '--port', args.port, '--phase', manifest.get('phase','voice-cloud'), '--build', str(PACKAGE)], check=True)
    print("Application installed and verified. You can now open the device chat.")


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, KeyError, subprocess.CalledProcessError) as exc:
        print(f"Install failed: {exc}", file=sys.stderr)
        raise SystemExit(1)
