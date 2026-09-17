"""M2 USB integration checks; media remains on the device, logs contain levels/status only."""
import argparse
import json
from pathlib import Path
import time
from hardware_faults import command
from serial_link import connect, USBReplyTimeout


class Device:
    def __init__(self, link, path):
        self.link, self.path = link, path
        self.report = {"passed": False, "cases": []}

    def save(self):
        self.path.parent.mkdir(parents=True, exist_ok=True)
        self.path.write_text(json.dumps(self.report, ensure_ascii=False, indent=2), encoding="utf-8")

    def run(self, text, expected="@ok", timeout=12):
        reply = command(self.link, text, timeout)
        self.report["cases"].append({"command": text, "response": reply})
        self.save()
        assert expected in reply, reply
        return reply

    def get(self, what):
        try:
            reply = command(self.link, "agent " + what, marker=b"}\r\n")
        except USBReplyTimeout as error:
            # Preserve the cause of a failed read-only poll; do not retry and
            # turn a lost/truncated response into an unreported passing case.
            if what in ("status", "wake status", "audio status", "mic status", "context stats"):
                self.report.setdefault("poll_failures", []).append(dict(
                    command="agent "+what, received_bytes=len(error.received),
                    response=error.received.decode("utf8", "replace")))
                self.save()
            raise
        return next(json.loads(line) for line in reply.splitlines() if line.startswith("{"))

    def plan(self, steps, **extra):
        return self.run("agent control run " + json.dumps({"steps": steps, **extra}, separators=(",", ":")))

    def wait(self, what, predicate, timeout=20):
        deadline = time.monotonic() + timeout
        while True:
            state = self.get(what)
            if predicate(state):
                return state
            if time.monotonic() >= deadline:
                raise TimeoutError(f"{what}: {state}")
            time.sleep(.04)


def basic(d):
    initial_audio = d.get("audio status")
    initial_light = d.get("light get")
    d.report["before"] = d.get("status")
    d.report["context_before"] = d.get("context stats")
    d.report["capabilities"] = d.get("control capabilities")
    try:
        d.run("agent audio stop")
        d.run("agent mic off")
        d.wait("audio status", lambda s: not s["playing"] and not s["mic_enabled"])
        d.run("agent audio volume 0")
        invalid = {"steps": [{"op": "light", "r": 200, "g": 0, "b": 0}, {"op": "gpio_write", "pin": 18, "value": 1}]}
        d.run("agent control run " + json.dumps(invalid), "@error forbidden")
        assert d.get("light get") == initial_light
        d.plan([{"op": "light", "r": 0, "g": 0, "b": 16}, {"op": "wait", "ms": 1000}], repeat=2)
        assert d.get("control status")["active"]
        d.run("agent light set 255 0 0", "@error busy")
        state = d.wait("control status", lambda s: not s["active"])
        assert state["state"] == "done" and state["cycles"] == 2, state
        assert d.get("light get") == initial_light
        d.report["repeat"] = state
        score = {"bpm": 60, "wave": "sine", "notes": [[69, 16]]}
        d.plan([{"op": "play_score", "score": 0, "wait": False}, {"op": "light", "r": 0, "g": 10, "b": 0},
            {"op": "await", "resource": "speaker", "timeout_ms": 5000}], scores=[score])
        d.wait("audio status", lambda s: s["playing"])
        started = time.monotonic()
        d.run("agent control cancel")
        state = d.wait("control status", lambda s: not s["active"], 2)
        d.report["cancel"] = {"state": state, "seconds": time.monotonic() - started}
        assert state["state"] == "cancelled" and d.report["cancel"]["seconds"] < 1.5
        assert not d.get("audio status")["playing"] and d.get("light get") == initial_light
        d.plan([{"op": "gpio_read", "pin": 0}])
        state = d.wait("control status", lambda s: not s["active"])
        opposite = 1 - state["last_value"]
        d.plan([{"op": "wait_gpio", "pin": 0, "value": opposite, "timeout_ms": 400}])
        state = d.wait("control status", lambda s: not s["active"])
        assert state["state"] == "failed" and state["error"] == "timeout", state
        d.report["gpio_timeout"] = state
        d.run("agent mic on")
        state = d.wait("audio status", lambda s: s["mic_valid"])
        d.report["fresh_mic"] = state
        d.run("agent mic off")
        state = d.wait("audio status", lambda s: not s["mic_enabled"])
        assert not state["mic_valid"]
        d.report["stopped_mic"] = state
        d.plan([{"op": "light", "r": 0, "g": 16, "b": 0}, {"op": "capture", "ms": 2000},
            {"op": "light", "r": 0, "g": 0, "b": 16}, {"op": "replay"}])
        samples = []
        deadline = time.monotonic() + 20
        while True:
            samples.append(d.get("audio status"))
            state = d.get("control status")
            if not state["active"]:
                break
            assert time.monotonic() < deadline, state
        d.report["capture_replay"] = {"state": state, "samples": samples}
        assert state["state"] == "done", state
        final = d.get("audio status")
        assert final["clip_ready"] and final["clip_ms"] == 2000 and final["capture_samples"] == 32000, final
        assert final["capture_error"] == final["play_error"] == "ok" and final["mic_overruns"] == 0, final
        assert final["rendered"] == final["total"] == 48000 and not final["mic_enabled"], final
        assert d.get("light get") == initial_light
        d.report["final_audio"] = final
        d.report["passed"] = True
    finally:
        d.run("agent control cancel")
        d.wait("control status", lambda s: not s["active"])
        d.run("agent audio stop")
        d.run("agent mic off")
        d.run("agent audio volume " + str(initial_audio["volume"]))
        d.run("agent light set {r} {g} {b}".format(**initial_light))
        d.report["after"] = d.get("status")
        d.save()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="COM5")
    parser.add_argument("--log", type=Path, default=Path("artifacts/logs/m2-hardware-basic.json"))
    args = parser.parse_args()
    with connect(args.port) as link:
        d = Device(link, args.log)
        try:
            basic(d)
        except Exception as error:
            d.report["passed"] = False
            d.report["error"] = str(error)
            d.save()
            raise
        print(json.dumps({"passed": d.report["passed"], "after": d.report["after"]}, ensure_ascii=False))


if __name__ == "__main__":
    main()
