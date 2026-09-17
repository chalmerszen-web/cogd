"""Run a real USB/Wi-Fi/DeepSeek smoke test without reading device credentials."""
import argparse
import json
from pathlib import Path
import time
from serial_link import connect, receive_until


def status(link):
    link.reset_input_buffer()
    link.write(b"agent status\n")
    data = receive_until(link, b'"firmware":"esp-hi-agent"', 5)
    for line in data.decode("utf-8", errors="replace").splitlines():
        if line.startswith('{"firmware":'):
            return json.loads(line)
    raise RuntimeError("Missing status JSON")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port")
    parser.add_argument("--stream", action="store_true")
    parser.add_argument("--prompt", default="Reply with exactly ESP_AGENT_OK")
    parser.add_argument("--log", default="artifacts/logs/deepseek-smoke.json")
    args = parser.parse_args()
    report = {"stream": args.stream, "prompt": args.prompt}
    with connect(args.port) as link:
        deadline = time.monotonic() + 40
        while True:
            report["before"] = status(link)
            if report["before"].get("wifi"):
                break
            if time.monotonic() >= deadline:
                raise TimeoutError("Wi-Fi did not connect")
            time.sleep(0.5)
        link.write(f"agent time set {int(time.time())}\n".encode())
        receive_until(link, b"@ok", 5)
        command = "agent chat " if args.stream else "agent chat --no-stream "
        started = time.monotonic()
        link.write((command + args.prompt + "\n").encode("utf-8"))
        reply = receive_until(link, (b"@done", b"@error"), 90)
        report["elapsed_s"] = round(time.monotonic() - started, 3)
        report["reply"] = reply.decode("utf-8", errors="replace")
        report["passed"] = b"@done" in reply
        report["after"] = status(link)
    path = Path(args.log)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps(report, ensure_ascii=False, indent=2))
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
