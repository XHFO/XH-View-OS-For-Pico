#!/usr/bin/env python3
"""共享家庭模型及真实本机HTTP/TLS契约，不访问生产账户。
Shared home model and real loopback HTTP/TLS contracts; never contact production accounts.
"""
from pathlib import Path
import http.server
import json
import os
import re
import socket
import ssl
import subprocess
import sys
import tempfile
import threading

ROOT = Path(__file__).resolve().parents[1]


def run():
    subprocess.run([sys.executable, str(ROOT / "tools/test_home_link_thin.py")], check=True)
    with tempfile.TemporaryDirectory(prefix="pico-home-link-") as temp:
        private = Path(temp)
        private.chmod(0o700)
        binary = private / "home-link-test"
        sources = [ROOT / "tools/home_link_host_test.c"] + [
            ROOT / "components/read_pico_home_link" / name
            for name in ["home_link_model.c", "home_link_json.c", "bambu_auth.c", "read_pico_home_link.c"]
        ]
        subprocess.run([
            os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra", "-Werror", "-g",
            "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
            "-I", str(ROOT / "components/read_pico_home_link/include"),
            "-I", str(ROOT / "managed_components/espressif__cjson/cJSON"),
            *map(str, sources), "-pthread", "-lm", "-o", str(binary),
        ], check=True, cwd=ROOT)
        subprocess.run([str(binary)], check=True)
        source = (ROOT / "tools/home_link_host_test.c").read_text()
        c_literals = re.search(r"static const char states\[\]\s*=\s*\{(.*?)\};",source,re.S).group(1)
        states = "".join(json.loads(m.group()) for m in re.finditer(r'"(?:\\.|[^"\\])*"', c_literals))
        json.loads(states)
        errors, observed = [], []

        class Handler(http.server.BaseHTTPRequestHandler):
            def log_message(self, *args):
                pass

            def reply(self, status, value):
                body = value.encode()
                self.send_response(status)
                self.send_header("Content-Type", "application/json")
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)

            def authorized(self):
                return self.headers.get("Authorization") == "Bearer ha-fixture"

            def do_GET(self):
                if not self.authorized():
                    self.reply(401, "{}")
                elif self.path == "/api/states":
                    observed.append("states")
                    self.reply(200, states)
                else:
                    self.reply(404, "{}")

            def do_POST(self):
                body = self.rfile.read(int(self.headers.get("Content-Length", "0")))
                if not self.authorized():
                    self.reply(401, "{}")
                elif self.path == "/api/template":
                    request = json.loads(body)
                    assert "area_name(id)" in request["template"]
                    assert request["variables"]["printers"] == []
                    observed.extend(["states", "rooms"])
                    self.reply(200, json.dumps(json.loads(states) + [{"pico_total": 6, "pico_cut": False}], ensure_ascii=False))
                elif self.path == "/api/services/light/turn_off":
                    assert json.loads(body)["entity_id"] == "light.desk"
                    observed.append("control")
                    self.reply(200, "[]")
                else:
                    self.reply(404, "{}")

        server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        env = dict(os.environ, RPICO_HOME_LINK_CONFIG=str(private / "settings.bin"))
        # 有意污染curlrc；--disable必须隔离宿主默认设置。
        # Deliberately poison curlrc; --disable must isolate host defaults.
        (private / ".curlrc").write_text('output = "' + str(private / "forbidden-output") + '"\ninsecure\nlocation\n')
        env["CURL_HOME"] = str(private)
        try:
            subprocess.run([str(binary), "http", f"http://127.0.0.1:{server.server_port}"], env=env, check=True, timeout=25)
        finally:
            server.shutdown()
            server.server_close()
            thread.join(timeout=2)
        assert {"states", "rooms", "control"} <= set(observed)
        assert not (private / "forbidden-output").exists()
        assert (private / "settings.bin").stat().st_mode & 0o777 == 0o600
        subprocess.run([str(binary),"storage-fail"],env=dict(env,RPICO_HOME_LINK_CONFIG="/dev/null/settings"),check=True)
        config = private / "certificate.cnf"
        config.write_text("[req]\nprompt=no\ndistinguished_name=dn\nx509_extensions=v3\n[dn]\nCN=localhost\n[v3]\nsubjectAltName=DNS:localhost\nbasicConstraints=critical,CA:TRUE\nkeyUsage=critical,digitalSignature,keyEncipherment,keyCertSign\nextendedKeyUsage=serverAuth\n")
        cert, key = private / "ca.pem", private / "key.pem"
        subprocess.run(["/usr/bin/openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "1", "-config", str(config), "-keyout", str(key), "-out", str(cert)], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.load_cert_chain(cert, key)
        listener = socket.socket()
        listener.bind(("127.0.0.1", 0))
        listener.listen(1)
        listener.settimeout(20)
        mqtt_seen = []

        def receive(stream, size):
            data = b""
            while len(data) < size:
                chunk = stream.recv(size - len(data))
                if not chunk:
                    raise EOFError()
                data += chunk
            return data

        def packet(stream):
            header = receive(stream, 1)[0]
            size, multiplier = 0, 1
            for _ in range(4):
                encoded = receive(stream, 1)[0]
                size += (encoded & 127) * multiplier
                if not encoded & 128:
                    return header, receive(stream, size)
                multiplier *= 128
            raise AssertionError("Invalid MQTT length")

        def mqtt_fixture():
            try:
                connection, _ = listener.accept()
                with context.wrap_socket(connection, server_side=True) as stream:
                    stream.settimeout(4)
                    header, body = packet(stream)
                    assert header == 0x10 and body[:7] == b"\0\4MQTT\4"
                    assert b"fixture-access-code" in body and b"bblp" in body
                    mqtt_seen.append("connect")
                    stream.sendall(b"\x20\x02\0\0")
                    header, body = packet(stream)
                    topic = b"device/FIXTURE123/report"
                    assert header == 0x82 and body[4:-1] == topic and body[-1] == 0
                    mqtt_seen.append("subscribe")
                    stream.sendall(b"\x90\x03" + body[:2] + b"\0")
                    report = b'{"print":{"gcode_state":"RUNNING","mc_percent":57}}'
                    payload = len(topic).to_bytes(2, "big") + topic + report
                    assert len(payload) < 128
                    stream.sendall(bytes([0x30, len(payload)]) + payload)
                    try:
                        while True:
                            header, _ = packet(stream)
                            assert header >> 4 != 3, "Printer control PUBLISH is forbidden"
                    except (EOFError, socket.timeout):
                        pass
            except Exception as exc:
                errors.append(repr(exc))

        mqtt_thread = threading.Thread(target=mqtt_fixture, daemon=True)
        mqtt_thread.start()
        try:
            subprocess.run([str(binary), "mqtt", str(listener.getsockname()[1]), str(cert)], env=env, check=True, timeout=25)
        finally:
            listener.close()
            mqtt_thread.join(timeout=5)
        assert not errors, errors
        assert mqtt_seen == ["connect", "subscribe"], mqtt_seen
        print("home-link: actual loopback HTTP, area template, control, curlrc isolation, private persistence and TLS MQTT passed")


if __name__ == "__main__":
    run()
