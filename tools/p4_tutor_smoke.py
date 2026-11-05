"""One bounded P4 teaching smoke test, using an explicit fixed test image.

Reads and confirms one question, asks about step two, and finishes. It does not
test real paper/camera accuracy, physical touch, five-minute idle or rechecking.
The existing speech/fixture helpers are reused without modifying the firmware.
"""
import argparse
import ipaddress
import json
import socket
import sys
import threading
import time
from datetime import datetime, timezone
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

from p4_voice_smoke import P4, Smoke, check_explicit_end, fixture_paths, require, sha


def tutor_case(smoke, report):
    device = smoke.device
    device.QUERY_FIELDS["agent tutor status"] = "teaching_state"
    case = report["cases"]["tutor"] = {"passed": False}

    def status():
        return smoke.query("agent tutor status")

    def tool(**kwargs):
        return smoke.command("agent tool device_tutor " + json.dumps(kwargs, ensure_ascii=False), query=True)

    def spoken(name, condition, minimum=0, timeout=55):
        mark = len(device.events)
        smoke.played(name)
        tool_result = smoke.wait(lambda: smoke.event("realtime_tool_result", mark,
                                                     lambda value: value.get("text") == "device_tutor"),
                                 timeout, "Teaching action was not acknowledged: " + name)
        answer = smoke.wait(lambda: smoke.event("realtime_answer", mark,
                                                lambda value: value["time_ms"] >= tool_result["time_ms"]
                                                and len(value.get("text", "")) >= minimum),
                            timeout, "Teaching answer missing: " + name)
        done = smoke.wait(lambda: smoke.event("realtime_playback_done", mark,
                                              lambda value: value["time_ms"] >= answer["time_ms"]),
                          timeout, "Teaching audio unfinished: " + name)
        state = smoke.wait(lambda: (value if condition(value := status()) else None),
                           20, "Teaching state mismatch after " + name)
        receipt = {"fixture": name, "tool_result": tool_result, "answer": answer,
                   "playback_done": done, "state": state}
        case.setdefault("turns", []).append(receipt)
        return receipt

    board_ip = smoke.query("agent status")["ip"]
    require(ipaddress.ip_address(board_ip).version == 4, "Teaching test expects board IPv4")
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as route:
        route.connect((board_ip, 9))
        local_ip = route.getsockname()[0]
    image = smoke.fixtures["question"].read_bytes()
    requests = case["fixture_requests"] = []

    class Handler(BaseHTTPRequestHandler):
        def do_GET(handler):
            if handler.path != "/question.jpg":
                handler.send_error(404)
                return
            if len(requests) < 32:
                requests.append({"path": handler.path, "at": time.monotonic(), "bytes": len(image)})
            handler.send_response(200)
            handler.send_header("Content-Type", "image/jpeg")
            handler.send_header("Content-Length", str(len(image)))
            handler.end_headers()
            try:
                handler.wfile.write(image)
            except (ConnectionError, OSError):
                pass

        def log_message(handler, *unused):
            pass

    server = ThreadingHTTPServer((local_ip, 0), Handler)
    worker = threading.Thread(target=server.serve_forever, kwargs={"poll_interval": .1}, daemon=True)
    worker.start()
    try:
        print("Starting fixed-image question (not a camera observation)", flush=True)
        case["cancel_previous"] = tool(action="cancel")
        case["start"] = tool(action="start", test_image_url=f"http://{local_ip}:{server.server_port}/question.jpg")
        question = smoke.wait(lambda: (value if (value := status()).get("tutor_state") == "awaiting_confirmation" else None),
                              70, "Fixed question did not reach confirmation")
        case["question"] = question
        question_id = question["question_id"]
        require(requests, "Board never fetched the fixed test image")
        session_mark, case["ready"] = smoke.wake()
        # Ask what was read before confirming, so confirmation is a subsequent real user turn.
        mark = len(device.events)
        smoke.played("question_status")
        case["readback"] = smoke.answer(mark, "17", 45)
        mark = len(device.events)
        smoke.played("confirm")
        solution = smoke.wait(lambda: (value if (value := status()).get("tutor_state") == "complete" else None),
                              150, "Confirmed question did not produce a complete solution")
        require(solution["question_id"] == question_id and solution["solution_bytes"] > 0,
                "Solution does not belong to the confirmed question")
        case["solution"] = solution
        revision = solution["revision"]
        result = smoke.wait(lambda: smoke.event("realtime_job_result", mark,
                                                lambda value: value.get("text") == "device_tutor"),
                            25, "Teaching job result was not delivered to realtime voice")
        choice = smoke.wait(lambda: smoke.event("realtime_answer", mark,
                                                lambda value: value["time_ms"] >= result["time_ms"]
                                                and "看" in value.get("text", "")
                                                and any(word in value.get("text", "") for word in ("讲", "听"))),
                            35, "Did not offer reading versus explanation after solving")
        choice_done = smoke.wait(lambda: smoke.event("realtime_playback_done", mark,
                                                     lambda value: value["time_ms"] >= choice["time_ms"]),
                                 35, "Choice prompt did not finish")
        choice_state = status()
        require(choice_state["teaching_state"] == "choice", "Solution did not enter choice state")
        case["choice"] = {"answer": choice, "playback_done": choice_done, "state": choice_state}
        print("Solution complete; asking about step two through the microphone", flush=True)
        step = spoken("step2", lambda value: value.get("teaching_state") == "explaining"
                      and value.get("current_step") == 2, minimum=25)
        require(step["state"]["question_id"] == question_id and step["state"]["revision"] == revision,
                "Step follow-up changed question or recomputed the solution")
        spoken("tutor_done", lambda value: value.get("teaching_state") == "done")
        case["done_state"] = status()
        require(case["done_state"]["question_id"] == question_id
                and case["done_state"]["revision"] == revision, "Finishing changed the retained lesson")
        end_mark = len(device.events)
        smoke.played("end")
        case.update(smoke.finish_session(session_mark))
        case["end_events"] = device.events[end_mark:]
        check_explicit_end(case["end_events"])
        require(case["final"]["realtime"]["interrupts"] == 0, "Unexpected interruption during teaching smoke")
        case["passed"] = True
        print("PASS fixed question, spoken confirmation, step-two follow-up and listening recovery", flush=True)
    finally:
        server.shutdown()
        server.server_close()
        worker.join(timeout=2)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--fixtures", type=Path, default=P4 / "fixtures/integration")
    parser.add_argument("--readback", type=Path)
    parser.add_argument("--output-device", type=int)
    parser.add_argument("--record", action="store_true")
    parser.add_argument("--record-device", default="麦克风 (Misiom-Shooter)")
    parser.add_argument("--volume", type=int, choices=range(0, 101), default=100, metavar="0..100")
    parser.add_argument("--timeout-seconds", type=int, default=300)
    args = parser.parse_args()
    require(60 <= args.timeout_seconds <= 600, "Timeout must be 60..600 seconds")
    args.out = args.out.resolve()
    args.out.mkdir(parents=True, exist_ok=False)
    report = {"passed": False, "port": args.port, "cases": {}, "source": "test_image_not_camera",
              "physical_audio": True, "camera_accuracy_tested": False, "physical_touch_tested": False,
              "five_minute_idle_tested": False, "independent_recording": bool(args.record),
              "started_utc": datetime.now(timezone.utc).isoformat()}
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
        report["flash_binding"] = "Independent pre/post Flash readback is external; version alone is insufficient"
        if args.readback:
            require(sha(args.readback) == digest and args.readback.stat().st_size == app.stat().st_size,
                    "Independent application readback differs from build")
            report["pretest_readback"] = {"path": str(args.readback.resolve()), "sha256": digest,
                                           "bytes": app.stat().st_size, "match": True}
        names = ["wake", "question", "question_status", "confirm", "step2", "tutor_done", "end"]
        fixtures, report["fixtures"] = fixture_paths(args.fixtures, names)
        smoke = Smoke(args, report, fixtures, description["project_version"])
        smoke.prepare()
        if args.record:
            from record_speaker import Recorder
            recorder = Recorder(args.out / "room.wav", seconds=args.timeout_seconds + 30, device=args.record_device)
            recorder.__enter__()
            report["recording_started"] = recorder.started
        tutor_case(smoke, report)
        require(sha(app) == digest, "Build image changed during test")
        report["passed"] = report["cases"]["tutor"]["passed"]
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
            if smoke.mutated and not report["passed"]:
                # Cancel only this test's lesson on failure; keep the failure and old evidence.
                try:
                    smoke.deadline = time.monotonic() + 25
                    smoke.command("agent voice off", timeout=15)
                    smoke.command('agent tool device_tutor {"action":"cancel"}', query=True)
                except Exception as exc:
                    report["cancel_error"] = f"{type(exc).__name__}: {exc}"
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
