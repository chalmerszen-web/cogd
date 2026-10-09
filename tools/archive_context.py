"""Export LOCAL context before reclaiming old records; never manufacture a cloud ACK."""
import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

from migrate_context import partitions, scan_context
from serial_link import connect, receive_until

ROOT = Path(__file__).resolve().parents[1]


def digest(data):
    return hashlib.sha256(data).hexdigest()


def save(path, data):
    if path.exists():
        if path.read_bytes() != data:
            raise ValueError(f"Refusing to replace archive file: {path.name}")
        return
    with path.open("xb") as handle:
        handle.write(data)
        handle.flush()
        os.fsync(handle.fileno())
    if path.read_bytes() != data:
        raise OSError("Archive readback differs")


def request(link, text, status=False):
    link.reset_input_buffer()
    link.write((text + "\n").encode("utf-8"))
    reply = receive_until(link, b"}\r\n" if status else (b"@ok", b"@error"), 45)
    if b"@error" in reply:
        raise RuntimeError(reply.decode("utf-8", "replace").strip())
    if status:
        for line in reply.splitlines():
            if line.startswith(b"{"):
                return json.loads(line)
        raise ValueError("Device status was not JSON")
    return reply.decode("utf-8", "replace").strip()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="COM5")
    parser.add_argument("--archive", type=Path, help="Reuse an existing verified context archive")
    parser.add_argument("--apply", action="store_true", help="Reclaim exported old records after durable verification")
    args = parser.parse_args()
    with connect(args.port) as link:
        device = request(link, "agent status", True)
        before = request(link, "agent context stats", True)
    if device["busy"] or not before["ready"] or before["mode"] != "LOCAL":
        raise ValueError("Device must be idle with ready LOCAL context")
    if before["partition_bytes"] != 0x200000 or "archived_record" not in before:
        raise ValueError("Requires current layout and firmware 0.6.3-context or newer")
    archive = args.archive
    if archive is None:
        stamp = datetime.datetime.now().strftime("%Y-%m-%d-%H%M%S-%f")
        archive = ROOT / "context-archives" / stamp
        archive.mkdir(parents=True, exist_ok=False)
        command = [sys.executable, "-m", "esptool", "--chip", "esp32c3", "--port", args.port,
                   "--baud", "460800", "read-flash"]
        with (archive / "read.log").open("w", encoding="utf-8") as log:
            subprocess.run(command + ["0x8000", "0x1000", str(archive / "partition-table.bin")],
                           stdout=log, stderr=subprocess.STDOUT, check=True)
            table = partitions((archive / "partition-table.bin").read_bytes())
            if (table["ctx"]["offset"], table["ctx"]["size"]) != (0x190000, 0x200000):
                raise ValueError("Unexpected device context partition")
            subprocess.run(command + ["0x190000", "0x200000", str(archive / "context.bin")],
                           stdout=log, stderr=subprocess.STDOUT, check=True)
    image = (archive / "context.bin").read_bytes()
    if len(image) != 0x200000:
        raise ValueError("Expected a complete 2 MiB context partition")
    scan = scan_context(image)
    if scan["tail_recovered"]:
        raise ValueError("Recover the incomplete WAL tail before archival")
    events = [r for r in scan["records"] if r["kind"] == 1]
    exported = b"".join(r["body"] + b"\n" for r in events)
    identity = device["device_id"]
    if not any(json.loads(r["body"])["device_id"] == identity for r in events):
        raise ValueError("Archive has no events from this device")
    manifest_path = archive / "manifest.json"
    through = max(r["seq"] for r in scan["records"])
    if manifest_path.exists():
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        if (manifest["device_id"] != identity or manifest["context_sha256"] != digest(image) or
                manifest["generation"] != scan["generation"] or manifest["through_record"] != through or
                manifest["events_sha256"] != digest(exported)):
            raise ValueError("Archive manifest verification failed")
    else:
        manifest = {"device_id": identity, "context_sha256": digest(image),
                    "generation": scan["generation"], "through_record": through,
                    "events": len(events), "events_sha256": digest(exported)}
    save(archive / "events.jsonl", exported)
    if not manifest_path.exists():
        save(manifest_path, (json.dumps(manifest, indent=2) + "\n").encode())
    # Force the original binary export to disk as well as verifying its CRCs.
    with (archive / "context.bin").open("r+b") as handle:
        handle.flush()
        os.fsync(handle.fileno())
    print(f"Verified {len(events)} events in {archive.resolve()}")
    if not args.apply:
        print("Archive saved. Use --archive <this directory> --apply to reclaim the exported prefix.")
        return
    with connect(args.port) as link:
        current_device = request(link, "agent status", True)
        current = request(link, "agent context stats", True)
        if current_device["device_id"] != identity or current_device["busy"] or current["mode"] != "LOCAL":
            raise ValueError("Device identity, mode or activity changed")
        command = "agent context archive " + json.dumps({"generation": scan["generation"], "through_record": through})
        reply = request(link, command)
        after = request(link, "agent context stats", True)
    result = {"archive": str(archive.resolve()), "reply": reply, "before": current, "after": after}
    result["passed"] = (after["archived_record"] >= through and after["acked"] == current["acked"] and
                        after["history_budget"] == current["history_budget"] and
                        after["recent_turns"] == current["recent_turns"])
    save(archive / ("result-" + datetime.datetime.now().strftime("%Y%m%d-%H%M%S-%f") + ".json"),
         (json.dumps(result, indent=2) + "\n").encode())
    if not result["passed"]:
        raise RuntimeError("Archive operation requires inspection; original export is intact")
    print(f"Context ready: {after['bank_size'] - after['used']} bytes free, {after['recent_turns']} recent turns retained")


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, RuntimeError, KeyError, subprocess.CalledProcessError) as error:
        print(f"Context archival stopped: {error}", file=sys.stderr)
        raise SystemExit(1)
