"""Bounded P4 integration checks using real microphone input and fixed fixtures.

This tool never flashes a device. Bind its build hash to independent Flash readback
before and after the run. Fixed speaker replay is not human/room acceptance;
media taps are software dispatch, and media is tested separately from voice.
"""
import argparse
import concurrent.futures
import hashlib
import ipaddress
import json
import re
import socket
import sys
import threading
import time
from datetime import datetime, timezone
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
P4 = ROOT / "ESP32-P4-WIFI6-Touch-LCD-3.5"
sys.path[:0] = [str(ROOT / "tools"), str(ROOT / "tools/voice")]


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def check_explicit_end(events):
    require(not any(v["stage"] == "realtime_idle_farewell" for v in events),
            "Explicit end was replaced by automatic idle farewell")
    require(any(v["stage"] == "realtime_user" and any(word in v.get("text", "")
                for word in ("再见", "聊到这里", "结束")) for v in events),
            "Explicit end was not transcribed")
    replies = [v for v in events if v["stage"] == "realtime_answer" and v.get("text")]
    require(replies, "Explicit end did not receive its own reply")
    require(any(v["stage"] == "realtime_playback_done" and v["time_ms"] >= replies[-1]["time_ms"]
                for v in events), "Explicit end reply did not finish playing")


def fixture_paths(directory, names):
    directory = directory.resolve()
    manifest = json.loads((directory / "manifest.json").read_text(encoding="utf8"))
    paths, receipts = {}, {}
    for name in names:
        item = manifest["files"][name]
        path = (directory / item["file"]).resolve()
        require(path.is_relative_to(directory), "Fixture must stay within --fixtures")
        digest = sha(path)
        require(digest == item["sha256"] and path.stat().st_size == item["bytes"],
                "Fixture checksum mismatch: " + name)
        paths[name] = path
        receipts[name] = {"file": item["file"], "sha256": digest, "bytes": path.stat().st_size}
    return paths, receipts


class Smoke:
    def __init__(self, args, report, fixtures, version):
        from device import Device
        Device.QUERY_FIELDS.update({"agent voice realtime": "uploaded_samples",
                                    "agent media status": "state"})
        self.args, self.report, self.fixtures, self.version = args, report, fixtures, version
        self.device = Device(args.out / "usb", args.port)
        self.device.link.timeout = .02
        self.deadline = time.monotonic() + args.timeout_seconds
        self.mutated = False
        self.testing = False
        self.original_volume = None

    def fault_check(self, lines):
        for line in lines:
            if any(token in line for token in ("Guru Meditation", "assert failed", "Stack protection")):
                raise RuntimeError("Firmware fault; inspect usb/serial.log")
            if self.testing and "ESP-ROM:" in line:
                raise RuntimeError("Unexpected device reboot; inspect usb/serial.log")

    def pump(self):
        require(time.monotonic() < self.deadline, "Overall test deadline exceeded")
        self.fault_check(self.device.lines())

    def command(self, text, query=False, timeout=6):
        require(time.monotonic() < self.deadline, "Overall test deadline exceeded")
        result = self.device.command(text, query=query, timeout=timeout, cancelled_ok=True)
        self.fault_check(self.device.rows[-1].get("lines", []))
        return result

    def query(self, text):
        # Only read-only state queries may be retried. Tool POST/tap commands are never retried.
        require(not text.startswith("agent tool ") or text == 'agent tool qianwen_skill {"action":"status"}',
                "Mutation submitted through read-only retry helper")
        until = min(self.deadline, time.monotonic() + 18)
        timeouts = 0
        while time.monotonic() < until:
            try:
                return self.command(text, query=True)
            except TimeoutError:
                timeouts += 1
                if timeouts >= 3:
                    raise
            except RuntimeError as exc:
                if "@error busy" not in str(exc):
                    raise
                self.hold(.15)
        raise TimeoutError("Read-only state query stayed busy: " + text)

    def wait(self, predicate, seconds, description):
        until = min(self.deadline, time.monotonic() + seconds)
        while time.monotonic() < until:
            self.pump()
            value = predicate()
            if value:
                return value
            time.sleep(.03)
        raise TimeoutError(description)

    def hold(self, seconds):
        until = time.monotonic() + seconds
        while time.monotonic() < until:
            self.pump()
            time.sleep(.03)

    def event(self, stage, mark, predicate=lambda value: True):
        return next((v for v in self.device.events[mark:]
                     if v["stage"] == stage and predicate(v)), None)

    def played(self, name, gain=1):
        from wave_play import play
        with concurrent.futures.ThreadPoolExecutor(max_workers=1) as pool:
            future = pool.submit(play, self.fixtures[name], device=self.args.output_device,
                                 gain=gain, preroll_ms=200)
            while not future.done():
                self.pump()
                time.sleep(.01)
            receipt = future.result()
        self.report.setdefault("sources", []).append({"fixture": name, "gain": gain, "playback": receipt})
        return receipt

    def snapshot(self):
        return {"status": self.query("agent status"), "wake": self.query("agent wake status"),
                "realtime": self.query("agent voice realtime"), "audio": self.query("agent audio status")}

    def prepare(self):
        self.report["before"] = self.snapshot()
        state = self.report["before"]
        require(state["status"]["version"] == self.version, "Device/build version mismatch")
        require(state["wake"]["model"] == "wn10_nihaoxiaozhi", "Expected official P4 wake model")
        self.original_volume = state["audio"].get("volume")
        self.testing = True
        self.mutated = True
        self.command("agent voice off", timeout=20)
        self.wait(lambda: not self.query("agent status")["busy"], 25, "Device did not become idle")
        self.wait(lambda: self.query("agent status")["wifi"], 45, "Device Wi-Fi unavailable")
        self.wait(lambda: self.query("agent status")["time_valid"], 45,
                  "Device did not synchronize its own clock after Wi-Fi connected")
        self.command("agent voice mode natural")
        self.command("agent audio volume " + str(self.args.volume))

    def wake(self):
        self.command("agent voice on")
        self.wait(lambda: self.query("agent wake status")["state"] == "listening", 20,
                  "Official wake engine did not become ready")
        mark = len(self.device.events)
        self.played("wake", .35)
        ready = self.wait(lambda: (v if (v := self.query("agent voice realtime"))["ready"] else None),
                          20, "Single acoustic wake did not open realtime session")
        return mark, ready

    def answer(self, mark, word, seconds=40):
        answer = self.wait(lambda: self.event("realtime_answer", mark,
                                              lambda v: word in v.get("text", "")),
                           seconds, "Expected answer missing: " + word)
        done = self.wait(lambda: self.event("realtime_playback_done", mark,
                                            lambda v: v["time_ms"] >= answer["time_ms"]),
                         seconds, "Answer audio did not finish")
        return {"answer": answer, "playback_done": done}

    def finish_session(self, mark, require_farewell=False):
        # Vendor MODEL_LOADER logs can interleave with the diagnostic JSON at
        # rearm. The authoritative state queries must prove closure even when
        # that optional event is malformed; never invent a replacement event.
        self.wait(lambda: not self.query("agent voice realtime")["active"], 60,
                  "Realtime session did not close")
        self.wait(lambda: self.query("agent wake status")["state"] == "listening", 20,
                  "Wake listening did not recover")
        closed = self.event("realtime_closed", mark)
        if closed:
            require(closed.get("text") == "ok", "Realtime closed with error: " + str(closed))
        closed_observed = time.monotonic()
        final = self.snapshot()
        require(final["status"]["version"] == self.version, "Device version changed")
        require(not final["realtime"]["active"] and not final["realtime"]["ready"],
                "Realtime session still active")
        require(final["realtime"]["error"] == "ok", "Realtime state reports a failure")
        if require_farewell:
            require(self.event("realtime_idle_farewell", mark), "Idle farewell was not observed")
        for key in ("stream_underruns", "tx_deadline_misses", "echo_probe_resumes"):
            require(key in final["realtime"] and final["realtime"][key] == 0,
                    "Audio continuity check failed: " + key)
        require(not any(v["stage"] in ("realtime_provider_failed", "realtime_provider_error")
                        for v in self.device.events[mark:]), "Provider failure in session")
        return {"closed": closed, "closed_event_missing": closed is None,
                "closed_state_observed": closed_observed, "final": final}

    def voice(self):
        cases = self.report.setdefault("cases", {})
        chat = cases["chat_followup"] = {"passed": False}
        print("Running official wake, chat and follow-up", flush=True)
        mark, chat["ready"] = self.wake()
        first = len(self.device.events)
        self.played("short")
        chat["first"] = self.answer(first, "蓝莓")
        second = len(self.device.events)
        self.played("recall")
        chat["followup"] = self.answer(second, "蓝莓")
        chat.update(self.finish_session(mark, require_farewell=True))
        require(chat["final"]["realtime"]["interrupts"] == 0, "False interruption in ordinary chat")
        chat["passed"] = True
        print("PASS chat and follow-up; running a fresh wake and interruption", flush=True)
        barge = cases["barge"] = {"passed": False}
        mark, barge["ready"] = self.wake()
        story = len(self.device.events)
        self.played("newstory")
        barge["speaker_start"] = self.wait(lambda: self.event("realtime_speaker_start", story),
                                            35, "Long reply did not start")
        self.hold(1.1)  # Respect the current firmware's 900 ms AEC startup guard.
        barge["before_interrupt"] = self.query("agent audio status")
        require(barge["before_interrupt"]["playing"], "Long reply ended before interruption source")
        interrupt_mark = len(self.device.events)
        receipt = self.played("interrupt2", self.args.barge_gain)
        cut = self.wait(lambda: self.event("realtime_interrupt", interrupt_mark,
                                           lambda v: v.get("text") == "local_vad"),
                        15, "No real local-VAD interruption was observed")
        require(cut["observed"] >= receipt["source_started"], "Interruption preceded test source")
        barge["interrupt"] = cut
        barge["source_start_to_observed_cut_ms"] = round((cut["observed"] - receipt["source_started"]) * 1000)
        barge["reply"] = self.answer(interrupt_mark, "松果", 45)
        end_mark = len(self.device.events)
        self.played("end")
        barge.update(self.finish_session(mark))
        require(barge["final"]["realtime"]["interrupts"] == 1, "Expected exactly one deliberate interruption")
        barge["end_events"] = self.device.events[end_mark:]
        check_explicit_end(barge["end_events"])
        barge["passed"] = True
        print("PASS interruption and return to listening", flush=True)

    def media(self):
        case = self.report.setdefault("cases", {})["media"] = {
            "passed": False, "physical_touch_tested": False, "concurrent_media_voice_tested": False}
        self.command("agent voice off", timeout=20)
        self.wait(lambda: not self.query("agent status")["busy"], 25, "Device busy before media")
        board_ip = self.query("agent status")["ip"]
        require(ipaddress.ip_address(board_ip).version == 4, "Media test expects board IPv4")
        # UDP connect chooses the actual route to this board; it does not send a packet.
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as route:
            route.connect((board_ip, 9))
            local_ip = route.getsockname()[0]
        movie = self.fixtures["movie"].read_bytes()
        requests = case["http_requests"] = []

        class Handler(BaseHTTPRequestHandler):
            def do_GET(handler):
                if handler.path != "/movie.mp4":
                    handler.send_error(404)
                    return
                start, end, status = 0, len(movie) - 1, 200
                if value := handler.headers.get("Range"):
                    match = re.fullmatch(r"bytes=(\d+)-(\d*)", value)
                    if not match:
                        handler.send_error(416)
                        return
                    start = int(match[1])
                    end = min(end, int(match[2]) if match[2] else end)
                    status = 206
                if start > end:
                    handler.send_error(416)
                    return
                if len(requests) < 512:
                    requests.append({"start": start, "end": end, "status": status})
                handler.send_response(status)
                handler.send_header("Content-Type", "video/mp4")
                handler.send_header("Content-Length", str(end - start + 1))
                handler.send_header("Accept-Ranges", "bytes")
                if status == 206:
                    handler.send_header("Content-Range", f"bytes {start}-{end}/{len(movie)}")
                handler.end_headers()
                try:
                    handler.wfile.write(movie[start:end + 1])
                except (ConnectionError, OSError):
                    pass

            def log_message(handler, *unused):
                pass

        server = ThreadingHTTPServer((local_ip, 0), Handler)
        thread = threading.Thread(target=server.serve_forever, kwargs={"poll_interval": .1}, daemon=True)
        thread.start()
        try:
            url = f"http://{local_ip}:{server.server_port}/movie.mp4"
            case["url"] = url
            print("Running exact-byte local media playback", flush=True)
            request = json.dumps({"url": url, "fps": 15}, separators=(",", ":"))
            case["submitted"] = self.command("agent tool device_display_video " + request, query=True)
            case["result"] = self.wait(
                lambda: (v if (v := self.query('agent tool qianwen_skill {"action":"status"}')).get("state")
                         in ("played", "failed", "prepare_failed", "error") else None),
                180, "Local media did not complete")
            require(case["result"]["state"] == "played" and case["result"].get("decoded_frames", 0) > 0,
                    "Local media preparation/playback failed")
            taps = case["software_taps"] = []
            for unused in range(3):
                self.command("agent media tap")
                self.wait(lambda: (v := self.query("agent media status"))["state"] == "playing" and v["position_ms"] > 0,
                          5, "Cached replay did not start")
                self.command("agent media tap")
                paused = self.wait(lambda: (v if (v := self.query("agent media status"))["state"] == "paused" else None),
                                   4, "Software pause did not take effect")
                self.hold(.5)
                held = self.query("agent media status")
                require(held["state"] == "paused" and held["position_ms"] == paused["position_ms"],
                        "Playback advanced while paused")
                self.command("agent media tap")
                ended = self.wait(lambda: (v if (v := self.query("agent media status"))["state"] == "ended" else None),
                                  15, "Cached replay did not finish")
                taps.append({"paused": paused, "held": held, "ended": ended})
            self.command("agent voice on")
            self.wait(lambda: self.query("agent wake status")["state"] == "listening", 20,
                      "Voice did not recover after media")
            case["after"] = self.snapshot()
            case["passed"] = True
            print("PASS local media and software pause/resume", flush=True)
        finally:
            server.shutdown()
            server.server_close()
            thread.join(timeout=2)

    def cleanup(self):
        # A failed deadline still leaves a bounded opportunity to restore listening.
        self.deadline = time.monotonic() + 55
        try:
            if self.mutated:
                self.command("agent voice off", timeout=15)
                self.wait(lambda: not self.query("agent status")["busy"], 20, "Cleanup remained busy")
                if self.original_volume is not None:
                    self.command("agent audio volume " + str(self.original_volume))
                self.command("agent voice on", timeout=10)
                self.wait(lambda: self.query("agent wake status")["state"] == "listening", 20,
                          "Cleanup failed to restore listening")
                self.report["after"] = self.snapshot()
        finally:
            self.report["events"] = self.device.events
            try:
                self.device.save()
            finally:
                self.device.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--fixtures", type=Path, default=P4 / "fixtures/integration")
    parser.add_argument("--mode", choices=("voice", "media", "all"), default="voice")
    parser.add_argument("--readback", type=Path, help="Independent pre-test application readback; exact build length")
    parser.add_argument("--output-device", type=int, help="Windows output index; otherwise resolve Misiom-Shooter")
    parser.add_argument("--record", action="store_true", help="Record external microphone evidence using FFmpeg")
    parser.add_argument("--record-device", default="麦克风 (Misiom-Shooter)")
    parser.add_argument("--volume", type=int, choices=range(0, 101), default=100, metavar="0..100")
    parser.add_argument("--barge-gain", type=float, default=1)
    parser.add_argument("--timeout-seconds", type=int, default=600)
    args = parser.parse_args()
    require(60 <= args.timeout_seconds <= 900, "Timeout must be 60..900 seconds")
    require(0 < args.barge_gain <= 1, "Barge gain must be above zero and at most one")
    args.out = args.out.resolve()
    args.out.mkdir(parents=True, exist_ok=False)
    report = {"passed": False, "mode": args.mode, "port": args.port,
              "started_utc": datetime.now(timezone.utc).isoformat(), "cases": {},
              "scope": "Fixed physical speaker-to-microphone replay; no human accuracy or acoustic P95 claim",
              "independent_recording": bool(args.record), "concurrent_media_voice_tested": False}
    smoke, recorder = None, None
    began = time.monotonic()
    try:
        build = args.build_dir.resolve()
        description = json.loads((build / "project_description.json").read_text(encoding="utf8"))
        require(description["target"] == "esp32p4" and description["project_name"] == "cogd_p4",
                "Build directory is not the COGD P4 project")
        app = build / "cogd_p4.bin"
        digest = sha(app)
        report["build"] = {"version": description["project_version"], "sha256": digest,
                           "bytes": app.stat().st_size, "path": str(app)}
        report["flash_binding"] = "External readback required; runtime version alone does not prove image identity"
        if args.readback:
            require(sha(args.readback) == digest and args.readback.stat().st_size == app.stat().st_size,
                    "Independent application readback differs from build")
            report["pretest_readback"] = {"path": str(args.readback.resolve()), "sha256": digest,
                                           "bytes": app.stat().st_size, "match": True}
            report["flash_binding"] = "Independent pre-test readback matches; post-test readback is external"
        names = []
        if args.mode in ("voice", "all"):
            names += ["wake", "short", "recall", "newstory", "interrupt2", "end"]
        if args.mode in ("media", "all"):
            names += ["movie"]
        fixtures, report["fixtures"] = fixture_paths(args.fixtures, names)
        smoke = Smoke(args, report, fixtures, description["project_version"])
        smoke.prepare()
        if args.record:
            from record_speaker import Recorder
            recorder = Recorder(args.out / "room.wav", seconds=args.timeout_seconds + 30, device=args.record_device)
            recorder.__enter__()
            report["recording_started"] = recorder.started
        if args.mode in ("voice", "all"):
            smoke.voice()
        if args.mode in ("media", "all"):
            smoke.media()
        require(sha(app) == digest, "Build image changed during test")
        report["passed"] = all(case["passed"] for case in report["cases"].values())
    except Exception as exc:
        report["error"] = f"{type(exc).__name__}: {exc}"
    finally:
        if recorder and recorder.process:
            try:
                recorder.__exit__(None, None, None)
            except Exception as exc:
                report["recording_error"] = f"{type(exc).__name__}: {exc}"
                report["passed"] = False
        if smoke:
            try:
                smoke.cleanup()
            except Exception as exc:
                report["cleanup_error"] = f"{type(exc).__name__}: {exc}"
                report["passed"] = False
        report["elapsed_seconds"] = round(time.monotonic() - began, 2)
        report["completed_utc"] = datetime.now(timezone.utc).isoformat()
        (args.out / "report.json").write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf8")
    print(json.dumps({key: report[key] for key in ("passed", "error", "cleanup_error", "recording_error", "elapsed_seconds")
                      if key in report}, ensure_ascii=False), flush=True)
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    sys.exit(main())
