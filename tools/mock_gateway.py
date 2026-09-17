"""Development-only verified-HTTPS/SQLite gateway. No remote administration endpoint."""
import argparse
import hashlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import ipaddress
import json
import os
from pathlib import Path
import re
import secrets
import sqlite3
import ssl
import threading
import time
from urllib.parse import parse_qs, urlsplit

MAX_BODY = 24576
MAX_SEQ = 9007199254740991

def canonical(value):
    return json.dumps(value, ensure_ascii=False, separators=(",", ":"), sort_keys=True)

def certificates(directory, hosts):
    from cryptography import x509
    from cryptography.hazmat.primitives import hashes, serialization
    from cryptography.hazmat.primitives.asymmetric import ec
    from cryptography.x509.oid import NameOID
    from datetime import datetime, timedelta, timezone
    directory.mkdir(parents=True, exist_ok=True)
    if (directory / "server.pem").exists():
        return
    now = datetime.now(timezone.utc)
    ca_key, server_key = ec.generate_private_key(ec.SECP256R1()), ec.generate_private_key(ec.SECP256R1())
    name = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, "ESP Agent Local Test CA")])
    ca = (x509.CertificateBuilder().subject_name(name).issuer_name(name).public_key(ca_key.public_key())
          .serial_number(x509.random_serial_number()).not_valid_before(now-timedelta(days=1))
          .not_valid_after(now+timedelta(days=365)).add_extension(x509.BasicConstraints(ca=True, path_length=0), True)
          .add_extension(x509.KeyUsage(False, False, False, False, False, True, True, False, False), True)
          .sign(ca_key, hashes.SHA256()))
    names = []
    for host in sorted(set(hosts + ["localhost", "127.0.0.1"])):
        try:
            names.append(x509.IPAddress(ipaddress.ip_address(host)))
        except ValueError:
            names.append(x509.DNSName(host))
    server = (x509.CertificateBuilder().subject_name(x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, "ESP Agent Mock")]))
              .issuer_name(name).public_key(server_key.public_key()).serial_number(x509.random_serial_number())
              .not_valid_before(now-timedelta(days=1)).not_valid_after(now+timedelta(days=30))
              .add_extension(x509.SubjectAlternativeName(names), False)
              .add_extension(x509.BasicConstraints(ca=False, path_length=None), True)
              .add_extension(x509.ExtendedKeyUsage([x509.oid.ExtendedKeyUsageOID.SERVER_AUTH]), False)
              .sign(ca_key, hashes.SHA256()))
    for filename, cert in [("ca.pem", ca), ("server.pem", server)]:
        (directory / filename).write_bytes(cert.public_bytes(serialization.Encoding.PEM))
    for filename, key in [("ca.key", ca_key), ("server.key", server_key)]:
        (directory / filename).write_bytes(key.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8,
                                                           serialization.NoEncryption()))

class Gateway:
    def __init__(self, state):
        self.state = Path(state)
        self.state.mkdir(parents=True, exist_ok=True)
        self.lock = threading.RLock()
        self.db = sqlite3.connect(self.state / "context.db", check_same_thread=False)
        self.db.executescript("""
            PRAGMA journal_mode=WAL;
            PRAGMA synchronous=FULL;
            CREATE TABLE IF NOT EXISTS events(cursor INTEGER PRIMARY KEY AUTOINCREMENT, event_id TEXT UNIQUE NOT NULL,
                user_id TEXT NOT NULL, session_id TEXT NOT NULL, device_id TEXT NOT NULL, seq INTEGER NOT NULL, payload TEXT NOT NULL);
            CREATE TABLE IF NOT EXISTS acks(user_id TEXT,session_id TEXT,device_id TEXT,cursor INTEGER,
                PRIMARY KEY(user_id,session_id,device_id));
            CREATE TABLE IF NOT EXISTS turns(user_id TEXT,session_id TEXT,turn_id TEXT,round INTEGER,request TEXT,response TEXT,
                PRIMARY KEY(user_id,session_id,turn_id,round));
        """)
        token_path = self.state / "token"
        if not token_path.exists():
            token_path.write_text(secrets.token_urlsafe(24), encoding="ascii")
        self.token = token_path.read_text(encoding="ascii").strip()

    @staticmethod
    def scope(body):
        fields = tuple(body.get(k) for k in ("user_id", "session_id", "device_id"))
        if any(not isinstance(v, str) or not v or len(v.encode()) > 64 for v in fields):
            raise ValueError("invalid scope")
        if not re.fullmatch(r"[\w-]{1,32}", fields[2], flags=re.ASCII):
            raise ValueError("invalid device")
        return fields

    @staticmethod
    def event(event):
        Gateway.scope(event)
        seq, lamport = event.get("device_seq"), event.get("lamport")
        if type(seq) is not int or type(lamport) is not int or not 0 < seq <= MAX_SEQ or not 0 < lamport < MAX_SEQ:
            raise ValueError("invalid clocks")
        if event.get("schema") != "agent.context.event/1" or event.get("event_id") != f'{event["device_id"]}:{seq:020d}':
            raise ValueError("invalid event id")
        if event.get("type") not in ("message", "turn", "tool_result", "memory", "tombstone", "summary", "error"):
            raise ValueError("invalid type")
        if any(not isinstance(event.get(k), dict) for k in ("actor", "content", "policy")) or not isinstance(event.get("parents"), list) or len(event["parents"])>4:
            raise ValueError("invalid event body")
        if event["type"] in ("memory", "tombstone"):
            content = event["content"]
            if not isinstance(content.get("key"), str) or not 0 < len(content["key"].encode()) <= 64:
                raise ValueError("invalid key")
            if event["type"] == "memory" and (not isinstance(content.get("value"), str) or len(content["value"].encode()) > 128):
                raise ValueError("invalid value")
        if event["type"] == "summary":
            text, through = event["content"].get("text"), event["content"].get("through_seq")
            if not isinstance(text, str) or not 0 < len(text.encode()) <= 1536 or type(through) is not int or not 0 < through < seq:
                raise ValueError("invalid summary")

    def request(self, method, path, body):
        parsed = urlsplit(path)
        with self.lock, self.db:
            if method == "POST" and parsed.path == "/v1/context/events:batch":
                user, session, device = self.scope(body)
                events = body.get("events")
                if not isinstance(events, list) or not 1 <= len(events) <= 4:
                    raise ValueError("invalid batch")
                accepted, ack = 0, 0
                for event in events:
                    self.event(event)
                    if self.scope(event) != (user, session, device):
                        raise ValueError("cross-scope event")
                    text = canonical(event)
                    existing = self.db.execute("SELECT payload FROM events WHERE event_id=?", (event["event_id"],)).fetchone()
                    if existing and existing[0] != text:
                        raise ValueError("immutable event conflict")
                    if not existing:
                        self.db.execute("INSERT INTO events(event_id,user_id,session_id,device_id,seq,payload) VALUES(?,?,?,?,?,?)",
                                        (event["event_id"], user, session, device, event["device_seq"], text))
                        accepted += 1
                    ack = max(ack, event["device_seq"])
                return {"accepted": accepted, "acked_seq": ack}
            if method == "GET" and parsed.path == "/v1/context/events":
                query = {k: v[0] for k, v in parse_qs(parsed.query).items()}
                user, session, _ = self.scope(query)
                cursor = int(query.get("cursor", "0"))
                if not 0 <= cursor <= MAX_SEQ:
                    raise ValueError("invalid cursor")
                rows = self.db.execute("SELECT cursor,payload FROM events WHERE user_id=? AND session_id=? AND cursor>? ORDER BY cursor LIMIT 2",
                                       (user, session, cursor)).fetchall()
                events, size = [], 128
                for row_cursor, payload in rows:
                    if size + len(payload.encode()) + 2 > MAX_BODY:
                        break
                    events.append(json.loads(payload)); size += len(payload.encode()) + 2; cursor = row_cursor
                return {"events": events, "cursor": cursor, "more": bool(self.db.execute(
                    "SELECT 1 FROM events WHERE user_id=? AND session_id=? AND cursor>? LIMIT 1", (user, session, cursor)).fetchone())}
            if method == "POST" and parsed.path == "/v1/context/ack":
                user, session, device = self.scope(body)
                cursor = body.get("cursor")
                if type(cursor) is not int or not 0 <= cursor <= MAX_SEQ:
                    raise ValueError("invalid cursor")
                self.db.execute("INSERT INTO acks VALUES(?,?,?,?) ON CONFLICT(user_id,session_id,device_id) DO UPDATE SET cursor=max(cursor,excluded.cursor)",
                                (user, session, device, cursor))
                return {"ok": True, "cursor": cursor}
            if method == "POST" and parsed.path == "/v1/agent/turns":
                return self.turn(body)
        raise LookupError("unknown endpoint")

    def turn(self, body):
        user, session, _ = self.scope(body)
        turn_id, round_number, messages = body.get("turn_id"), body.get("round"), body.get("messages")
        if body.get("schema") != "agent.turn/1" or not isinstance(turn_id, str) or len(turn_id) > 64 or type(round_number) is not int or not 0 <= round_number <= 4:
            raise ValueError("invalid turn")
        if not isinstance(messages, list) or not messages or messages[0].get("role") != "user":
            raise ValueError("invalid messages")
        capabilities = body.get("capabilities")
        if type(body.get("stream")) is not bool or not isinstance(capabilities, list) or len(capabilities) > 32:
            raise ValueError("invalid capabilities or stream flag")
        available = set()
        for capability in capabilities:
            function = capability.get("function") if isinstance(capability, dict) else None
            name = function.get("name") if isinstance(function, dict) else None
            if not isinstance(capability, dict) or capability.get("type") != "function" or not isinstance(name, str) or not re.fullmatch(r"[A-Za-z0-9_-]{1,128}", name) or name in available:
                raise ValueError("invalid or duplicate capability")
            available.add(name)
        key = (user, session, turn_id, round_number)
        digest = hashlib.sha256(canonical(body).encode()).hexdigest()
        cached = self.db.execute("SELECT request,response FROM turns WHERE user_id=? AND session_id=? AND turn_id=? AND round=?", key).fetchone()
        if cached:
            if cached[0] != digest:
                raise ValueError("turn id conflict")
            return json.loads(cached[1])
        if round_number:
            previous = self.db.execute("SELECT response FROM turns WHERE user_id=? AND session_id=? AND turn_id=? AND round=?", key[:3]+(round_number-1,)).fetchone()
            if not previous:
                raise ValueError("missing preceding round")
            calls = json.loads(previous[0])["choices"][0]["message"].get("tool_calls", [])
            expected = [c["id"] for c in calls]
            actual = [m.get("tool_call_id") for m in messages[-len(expected):]] if expected else []
            if not expected or actual != expected or any(m.get("role") != "tool" for m in messages[-len(expected):]):
                raise ValueError("tool results must preserve all call ids")
        prompt = messages[0].get("content", "")
        if not isinstance(prompt, str) or len(prompt.encode()) > 2048:
            raise ValueError("invalid input")
        music = any(word in prompt.lower() for word in ("music", "melody", "音乐", "旋律"))
        tools = (not round_number and (music or any(word in prompt.lower() for word in ("light", "灯", "tools")))) or "[loop-tools]" in prompt
        message = {"role": "assistant", "content": "MOCK_LIGHT_OK" if round_number else "MOCK_OK"}
        if tools:
            rgb = {"r": 255, "g": 0, "b": 0}
            if "green" in prompt.lower() or "绿" in prompt:
                rgb = {"r": 0, "g": 255, "b": 0}
            names = ["device_light_set_rgb"]
            score = {"bpm": 120, "wave": "sine", "notes": [[60, 4], [64, 4], [67, 8]]}
            if music:
                names = ["device_audio_play_score"]
            elif "all tools" in prompt.lower():
                names = ["device_status_get", "device_light_get", "device_light_set_rgb", "agent_context_stats"]
            if any(name not in available for name in names):
                raise ValueError("requested tools are not device capabilities")
            message = {"role": "assistant", "content": None, "tool_calls": [
                {"id": f"mock-{round_number}-{i}", "type": "function", "function": {"name": name,
                    "arguments": canonical(score if name == "device_audio_play_score" else rgb if name == "device_light_set_rgb" else {})}}
                for i, name in enumerate(names)]}
        elif round_number and music:
            message["content"] = "MOCK_MUSIC_QUEUED"
        response = {"id": turn_id, "choices": [{"index": 0, "message": message, "finish_reason": "tool_calls" if tools else "stop"}]}
        self.db.execute("INSERT INTO turns VALUES(?,?,?,?,?,?)", key+(digest, canonical(response)))
        return response

def sse(response):
    message = response["choices"][0]["message"]
    frames = [": mock heartbeat\r\n\r\n"]
    def add(delta, finish=None):
        frames.append("data: " + canonical({"choices": [{"index": 0, "delta": delta, "finish_reason": finish}]}) + "\r\n\r\n")
    calls = message.get("tool_calls", [])
    for i, call in enumerate(calls):
        add({"tool_calls": [{"index": i, "id": call["id"], "type": "function", "function": {"name": call["function"]["name"], "arguments": ""}}]})
    for pos in range(max((len(c["function"]["arguments"]) for c in calls), default=0)):
        for i, call in enumerate(calls):
            if pos < len(call["function"]["arguments"]):
                add({"tool_calls": [{"index": i, "function": {"arguments": call["function"]["arguments"][pos]}}]})
    for ch in message.get("content") or "":
        add({"content": ch})
    add({}, response["choices"][0]["finish_reason"])
    frames.append("data: [DONE]\r\n\r\n")
    return "".join(frames).encode()

class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *_):
        pass  # Never log authentication headers or user messages.

    def do_GET(self):
        self.handle_request()

    def do_POST(self):
        self.handle_request()

    def send(self, status, payload, content_type="application/json", fragment=0, extra_length=0):
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(payload)+extra_length))
        self.send_header("Connection", "close")
        self.end_headers()
        if fragment:
            for i in range(0, len(payload), fragment):
                self.wfile.write(payload[i:i+fragment])
        else:
            self.wfile.write(payload)
        self.wfile.flush()
        self.close_connection = True

    def handle_request(self):
        gateway = self.server.gateway
        try:
            self.connection.settimeout(15)
            if not secrets.compare_digest(self.headers.get("Authorization", ""), "Bearer " + gateway.token):
                return self.send(401, b'{"error":"auth"}')
            length = int(self.headers.get("Content-Length", "0"))
            if length < 0 or length > MAX_BODY:
                return self.send(413, b'{"error":"limit"}')
            raw = self.rfile.read(length)
            def unique(pairs):
                obj = {}
                for k, v in pairs:
                    if k in obj:
                        raise ValueError("duplicate key")
                    obj[k] = v
                return obj
            body = json.loads(raw, object_pairs_hook=unique) if raw else {}
            fault_path = gateway.state / "fault.json"
            fault = json.loads(fault_path.read_text()) if fault_path.exists() else {}
            if fault.get("path") and not self.path.startswith(fault["path"]):
                fault = {}
            if fault.get("delay"):
                time.sleep(min(65, float(fault["delay"])))
            if fault.get("status"):
                return self.send(int(fault["status"]), b'{"error":"injected"}')
            if fault.get("mode") == "disconnect":
                self.close_connection = True
                return
            response = gateway.request(self.command, self.path, body)
            stream = self.path == "/v1/agent/turns" and body.get("stream", False)
            payload = sse(response) if stream else canonical(response).encode()
            if fault.get("mode") == "malformed":
                payload = b"data: {broken}\r\n\r\n" if stream else b"{broken}"
            if fault.get("mode") == "truncated":
                payload = payload[:max(1, len(payload)//2)]
            self.send(200, payload, "text/event-stream" if stream else "application/json",
                      fragment=int(fault.get("fragment", 7 if stream else 0)), extra_length=1 if fault.get("mode") == "truncated" else 0)
        except (ValueError, TypeError, KeyError, UnicodeError):
            self.send(400, b'{"error":"protocol"}')
        except LookupError:
            self.send(404, b'{"error":"not_found"}')
        except (BrokenPipeError, ConnectionError, TimeoutError, ssl.SSLError):
            self.close_connection = True

def create_server(state, host, port, cert_dir=None):
    gateway = Gateway(state)
    cert_dir = Path(cert_dir) if cert_dir else Path(state)
    certificates(cert_dir, [host] if host != "0.0.0.0" else [])
    server = ThreadingHTTPServer((host, port), Handler)
    server.daemon_threads = True
    server.gateway = gateway
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.minimum_version = ssl.TLSVersion.TLSv1_2
    context.load_cert_chain(cert_dir / "server.pem", cert_dir / "server.key")
    server.socket = context.wrap_socket(server.socket, server_side=True)
    return server

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--state", type=Path, default=Path(".local/mock"))
    parser.add_argument("--host", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=8443)
    parser.add_argument("--san", action="append", default=[])
    parser.add_argument("--cert-dir", type=Path)
    args = parser.parse_args()
    certificates(args.cert_dir or args.state, args.san)
    server = create_server(args.state, args.host, args.port, args.cert_dir)
    print(f"Mock Gateway HTTPS port {server.server_port}; credentials remain in {args.state}", flush=True)
    try:
        server.serve_forever()
    finally:
        server.server_close(); server.gateway.db.close()

if __name__ == "__main__":
    main()
