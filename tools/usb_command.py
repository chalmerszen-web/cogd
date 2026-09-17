"""Send diagnostic commands; never use this logger for provisioning secrets."""
import argparse
import json
from pathlib import Path
import time
from serial_link import connect, receive_until, USBReplyTimeout

def main():
    p = argparse.ArgumentParser()
    p.add_argument("command", nargs="+")
    p.add_argument("--port", default="COM5")
    p.add_argument("--timeout", type=float, default=20)
    p.add_argument("--log", type=Path)
    args = p.parse_args()
    results = []
    def save():
        if args.log:
            args.log.parent.mkdir(parents=True, exist_ok=True)
            args.log.write_text(json.dumps(results, ensure_ascii=False, indent=2), encoding="utf-8")
    with connect(args.port) as link:
        for command in args.command:
            if "provision" in command or command.startswith("agent wifi set"):
                raise SystemExit("Use provision_secret.py for credentials.")
            link.reset_input_buffer()
            link.write((command + "\n").encode())
            marker = (b"@ok", b"@error", b"@done", b"@reboot")
            if command.startswith("agent audio clip read ") or command in ("agent hardware", "agent display status", "agent wake status", "agent status", "agent wifi status", "agent wifi modem", "agent context stats", "agent light get", "agent audio status", "agent mic status", "agent control status", "agent control capabilities"):
                marker = b"}\r\n"
            try:
                response = receive_until(link, marker, args.timeout).decode("utf-8", "replace")
            except USBReplyTimeout as error:
                results.append({"command": command, "response": error.received.decode("utf-8", "replace"), "timeout": True})
                save()
                raise
            results.append({"command": command, "response": response})
            save()
            time.sleep(.15)
    text = json.dumps(results, ensure_ascii=False, indent=2)
    print(text)

if __name__ == "__main__":
    main()
