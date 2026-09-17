"""Real device fault tests. Configuration scratch contains no DeepSeek/Wi-Fi secrets."""
import argparse
import json
from pathlib import Path
import secrets
import time
from hardware_smoke import status
from serial_link import connect, receive_until

def command(link, text, timeout=30, marker=(b"@ok", b"@error", b"@done")):
    link.reset_input_buffer(); link.write((text+"\n").encode())
    return receive_until(link, marker, timeout).decode("utf-8", "replace")

def provision_url(link, url, ca):
    nonce = secrets.token_hex(8)
    payload = {"gateway_url": url, "gateway_ca": ca, "nonce": nonce, "epoch": int(time.time())}
    link.reset_input_buffer(); link.write(("@provision "+json.dumps(payload)+"\n").encode())
    reply = receive_until(link, (b"@provision_ok", b"@error"), 10)
    if b"@provision_ok" not in reply:
        raise RuntimeError("Diagnostic URL provisioning was not acknowledged")
    deadline = time.monotonic()+40
    while not status(link)["wifi"]:
        if time.monotonic()>deadline:
            raise TimeoutError("Wi-Fi reconnection")
        time.sleep(.25)

def main():
    p = argparse.ArgumentParser()
    p.add_argument("--port", default="COM5")
    p.add_argument("--gateway-url", default="https://192.168.9.109:8443")
    p.add_argument("--ca", type=Path, default=Path(".local/mock/ca.pem"))
    p.add_argument("--gateway", action="store_true", help="Also test the reachable mock fault matrix")
    args = p.parse_args()
    path = Path("artifacts/logs/phase-j-faults.json")
    report = {"cases": []}
    def record(name, reply, expected):
        passed = expected in reply
        report["cases"].append({"name": name, "reply": reply, "expected": expected, "passed": passed})
        path.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
        print(f"{name}: {'PASS' if passed else 'FAIL'}", flush=True)
    ca = args.ca.read_text(encoding="ascii")
    fault_path = Path(".local/mock/fault.json")
    with connect(args.port) as link:
        command(link, "agent context mode LOCAL")
        command(link, "agent time set " + str(int(time.time())))
        record("input_limit", command(link, "x"*2049), "@error limit")
        record("invalid_rgb", command(link, "agent light set 256 0 0"), "@error argument")
        command(link, "agent route GATEWAY")
        try:
            # A CA mismatch is rejected before any HTTP request or credential is sent.
            provision_url(link, "https://api.deepseek.com", ca)
            record("untrusted_ca", command(link, "agent chat TLS verification test", 50), "@error tls")
            provision_url(link, "https://agent-m0-unresolvable.invalid", ca)
            record("dns", command(link, "agent chat DNS test", 90), "@error dns")
            provision_url(link, args.gateway_url, ca)
            if args.gateway:
                for code, expected in [(401,"auth"),(403,"forbidden"),(429,"rate"),(500,"server"),(503,"server")]:
                    fault_path.write_text(json.dumps({"path":"/v1/agent/turns","status":code}))
                    record(f"http_{code}", command(link, "agent chat Fault matrix", 90), "@error "+expected)
                    if code==503:
                        time.sleep(31)  # Circuit recovery; bounded host wait, no USB command pending.
                for mode, expected in [("malformed","json"),("truncated","protocol"),("disconnect","network")]:
                    fault_path.write_text(json.dumps({"path":"/v1/agent/turns","mode":mode}))
                    record(mode, command(link,"agent chat Fault matrix",90),"@error "+expected)
                fault_path.write_text(json.dumps({"path":"/v1/agent/turns","delay":65}))
                record("response_timeout",command(link,"agent chat Fault timeout",170),"@error timeout")
                fault_path.unlink(missing_ok=True)
                record("round_limit", command(link,"agent chat [loop-tools]",90),"@error limit")
        finally:
            fault_path.unlink(missing_ok=True)
            provision_url(link, args.gateway_url, ca)
            command(link,"agent route DIRECT")
        link.reset_input_buffer()
        link.write(b"agent chat Write 200 numbered sentences about stars.\n")
        time.sleep(.15); link.write(b"agent cancel\n")
        record("cancel", receive_until(link,b"@error",30).decode("utf-8","replace"),"@error cancelled")
        link.reset_input_buffer()
        link.write(b"agent chat Write 200 numbered sentences about planets.\n")
        time.sleep(.2); link.write(b"agent wifi pause\n")
        reply=receive_until(link,(b"@error",b"@done"),90).decode("utf-8","replace")
        record("disconnect_during_turn",reply,"@error")
        report["offline_status"]=status(link)
        command(link,"agent wifi resume")
        deadline=time.monotonic()+40
        while not status(link)["wifi"]:
            if time.monotonic()>deadline:
                raise TimeoutError("Wi-Fi recovery")
            time.sleep(.5)
        record("reconnected_request",command(link,"agent chat Reply with exactly RECOVERED",90),"@done")
        report["after"]=status(link)
    report["passed"]=all(x["passed"] for x in report["cases"]) and not report["offline_status"]["wifi"]
    path.write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding="utf-8")
    return 0 if report["passed"] else 1

if __name__=="__main__":
    raise SystemExit(main())
