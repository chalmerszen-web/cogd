"""Discover the native ESP USB interface without resetting a device."""
import argparse
import json
from serial.tools import list_ports


def detect_port(requested=None):
    if requested:
        return requested
    candidates = [p for p in list_ports.comports() if p.vid == 0x303A and p.pid == 0x1001]
    if len(candidates) != 1:
        raise RuntimeError(f"Expected one ESP USB device, found {len(candidates)}; specify --port")
    return candidates[0].device


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port")
    args = parser.parse_args()
    print(json.dumps({"port": detect_port(args.port)}))
