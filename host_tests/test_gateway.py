import http.client
import json
from pathlib import Path
import ssl
import sys
import tempfile
import threading
import time
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from mock_gateway import create_server, Gateway

class GatewayTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory = tempfile.TemporaryDirectory()
        cls.server = create_server(cls.directory.name, "127.0.0.1", 0)
        cls.thread = threading.Thread(target=cls.server.serve_forever, daemon=True)
        cls.thread.start()
        cls.tls = ssl.create_default_context(cafile=str(Path(cls.directory.name) / "ca.pem"))

    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown(); cls.thread.join(); cls.server.server_close(); cls.server.gateway.db.close(); cls.directory.cleanup()

    def call(self, method, path, data=None, token=None, tls=None):
        c = http.client.HTTPSConnection("127.0.0.1", self.server.server_port, context=tls or self.tls, timeout=5)
        c.request(method, path, json.dumps(data) if data is not None else None,
                  {"Authorization": "Bearer " + (token or self.server.gateway.token), "Content-Type": "application/json"})
        r = c.getresponse(); value = r.read(); c.close()
        return r.status, value

    def test_auth_tls(self):
        self.assertEqual(self.call("POST", "/v1/context/ack", {}, token="invalid")[0], 401)
        with self.assertRaises(ssl.SSLCertVerificationError):
            self.call("GET", "/v1/context/events", tls=ssl.create_default_context())

    def test_context(self):
        event = {"schema": "agent.context.event/1", "event_id": "phone:00000000000000000001", "device_id": "phone",
                 "user_id": "local-user", "session_id": "default", "device_seq": 1, "lamport": 1,
                 "type": "memory", "actor": {"type": "user"}, "content": {"key": "color", "value": "blue"}, "policy": {}, "parents": []}
        body = {k: event[k] for k in ("device_id", "user_id", "session_id")}; body["events"] = [event]
        self.assertEqual(json.loads(self.call("POST", "/v1/context/events:batch", body)[1])["accepted"], 1)
        self.assertEqual(json.loads(self.call("POST", "/v1/context/events:batch", body)[1])["accepted"], 0)
        status, raw = self.call("GET", "/v1/context/events?cursor=0&user_id=local-user&session_id=default&device_id=phone")
        pulled = json.loads(raw); self.assertEqual(status, 200); self.assertEqual(pulled["events"], [event])
        ack = dict(body); ack.pop("events"); ack["cursor"] = pulled["cursor"]
        self.assertEqual(self.call("POST", "/v1/context/ack", ack)[0], 200)
        event["content"]["value"] = "conflict"
        self.assertEqual(self.call("POST", "/v1/context/events:batch", body)[0], 400)
        # Restarting the database connection retains uniqueness and cursor.
        reopened = Gateway(self.directory.name)
        self.assertEqual(reopened.db.execute("SELECT count(*) FROM events").fetchone()[0], 1)
        reopened.db.close()

    def test_turns(self):
        body = {"schema": "agent.turn/1", "device_id": "esp-test", "user_id": "u", "session_id": "s",
                "turn_id": "esp-test:00000000000000000001", "round": 0, "stream": False,
                "messages": [{"role": "user", "content": "Set all tools light green"}],
                "capabilities": [{"type":"function","function":{"name":name}} for name in
                    ("device_status_get","device_light_get","device_light_set_rgb","agent_context_stats")]}
        status, raw = self.call("POST", "/v1/agent/turns", body)
        self.assertEqual(status, 200); first = json.loads(raw)["choices"][0]["message"]
        self.assertEqual(len(first["tool_calls"]), 4)
        self.assertEqual(self.call("POST", "/v1/agent/turns", body)[1], raw)
        body["round"] = 1; body["stream"] = True; body["messages"].append(first)
        for call in first["tool_calls"]:
            body["messages"].append({"role": "tool", "tool_call_id": call["id"], "content": '{"ok":true}'})
        status, raw = self.call("POST", "/v1/agent/turns", body)
        self.assertEqual(status, 200); self.assertIn(b"data: [DONE]", raw)
        body["messages"][-1]["tool_call_id"] = "wrong"
        self.assertEqual(self.call("POST", "/v1/agent/turns", body)[0], 400)

    def test_music(self):
        names = ("device_status_get", "device_light_get", "device_light_set_rgb", "agent_context_stats",
                 "device_audio_get", "device_audio_play_score", "device_audio_stop", "device_audio_volume", "device_mic_set")
        body = {"schema": "agent.turn/1", "device_id": "test", "user_id": "music", "session_id": "music",
                "turn_id": "music:1", "round": 0, "stream": False,
                "messages": [{"role": "user", "content": "Compose music"}],
                "capabilities": [{"type": "function", "function": {"name": name}} for name in names]}
        status, raw = self.call("POST", "/v1/agent/turns", body)
        self.assertEqual(status, 200)
        message = json.loads(raw)["choices"][0]["message"]
        call = message["tool_calls"][0]
        self.assertEqual(call["function"]["name"], "device_audio_play_score")
        self.assertEqual(json.loads(call["function"]["arguments"])["notes"], [[60, 4], [64, 4], [67, 8]])
        body["round"] = 1
        body["messages"].extend([message, {"role": "tool", "tool_call_id": call["id"], "content": '{"playing":true}'}])
        self.assertIn(b"MOCK_MUSIC_QUEUED", self.call("POST", "/v1/agent/turns", body)[1])
        for invalid in ([None], [{"type": "function", "function": None}], body["capabilities"] * 2,
                        [body["capabilities"][0], body["capabilities"][0]]):
            body["capabilities"] = invalid
            self.assertEqual(self.call("POST", "/v1/agent/turns", body)[0], 400)

    def test_fault_matrix(self):
        fault=Path(self.directory.name)/"fault.json"
        body={"schema":"agent.turn/1","device_id":"test","user_id":"fault-user","session_id":"fault-session",
              "turn_id":"test:00000000000000000001","round":0,"stream":True,"capabilities":[],
              "messages":[{"role":"user","content":"Hello"}]}
        try:
            for code in (401,403,429,500,503):
                fault.write_text(json.dumps({"path":"/v1/agent/turns","status":code}))
                self.assertEqual(self.call("POST","/v1/agent/turns",body)[0],code)
            fault.write_text(json.dumps({"path":"/v1/agent/turns","mode":"malformed"}))
            self.assertIn(b"{broken}",self.call("POST","/v1/agent/turns",body)[1])
            fault.write_text(json.dumps({"path":"/v1/agent/turns","mode":"truncated"}))
            with self.assertRaises(http.client.IncompleteRead):
                self.call("POST","/v1/agent/turns",body)
            fault.write_text(json.dumps({"path":"/v1/agent/turns","mode":"disconnect"}))
            with self.assertRaises(http.client.RemoteDisconnected):
                self.call("POST","/v1/agent/turns",body)
            fault.write_text(json.dumps({"path":"/v1/agent/turns","delay":.05}))
            started=time.perf_counter()
            self.assertEqual(self.call("POST","/v1/agent/turns",body)[0],200)
            self.assertGreaterEqual(time.perf_counter()-started,.05)
        finally:
            fault.unlink(missing_ok=True)

    def test_m2_registry_summary(self):
        names = ("device_status_get", "device_light_get", "device_light_set_rgb", "agent_context_stats",
                 "device_audio_get", "device_audio_play_score", "device_audio_stop", "device_audio_volume", "device_mic_set",
                 "device_audio_capture", "device_audio_replay", "agent_context_search", "agent_context_summary_get",
                 "agent_context_summary_set", "device_control_capabilities", "device_control_validate", "device_control_run",
                 "device_control_status", "device_control_cancel")
        body = {"schema": "agent.turn/1", "device_id": "m2", "user_id": "m2-user", "session_id": "m2-session",
                "turn_id": "m2:1", "round": 0, "stream": False, "messages": [{"role": "user", "content": "Hello"}],
                "capabilities": [{"type": "function", "function": {"name": name}} for name in names]}
        self.assertEqual(self.call("POST", "/v1/agent/turns", body)[0], 200)
        event = {"schema": "agent.context.event/1", "event_id": "m2:00000000000000000002", "device_id": "m2",
                 "user_id": "m2-user", "session_id": "m2-session", "device_seq": 2, "lamport": 2, "type": "summary",
                 "actor": {"type": "assistant"}, "content": {"text": "Source-bound summary", "through_seq": 1}, "policy": {}, "parents": []}
        batch = {key: event[key] for key in ("device_id", "user_id", "session_id")}
        batch["events"] = [event]
        self.assertEqual(json.loads(self.call("POST", "/v1/context/events:batch", batch)[1])["accepted"], 1)
        self.assertEqual(json.loads(self.call("POST", "/v1/context/events:batch", batch)[1])["accepted"], 0)
        for invalid in (True, 0, 2):
            event["content"]["through_seq"] = invalid
            self.assertEqual(self.call("POST", "/v1/context/events:batch", batch)[0], 400)
        event["content"].update(through_seq=1, text="字" * 513)
        self.assertEqual(self.call("POST", "/v1/context/events:batch", batch)[0], 400)

if __name__ == "__main__":
    unittest.main(verbosity=2)
