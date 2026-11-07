#!/usr/bin/env python3
"""Local drag-and-drop editor for the Pico firmware UI layouts.

Run: python3 tools/ui-layout-editor/server.py --port 8787
The browser edits JSON under ./layouts. Nothing is written outside this folder.
"""
from __future__ import annotations

import argparse
import json
import mimetypes
import os
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import unquote, urlparse

ROOT = Path(__file__).resolve().parent
LAYOUTS = ROOT / "layouts"
MAX_BODY = 1_000_000


def safe_layout_id(value: str) -> str | None:
    value = unquote(value).strip()
    if not value or len(value) > 64:
        return None
    if any(c not in "abcdefghijklmnopqrstuvwxyz0123456789-_" for c in value):
        return None
    return value


def layout_path(layout_id: str) -> Path:
    return LAYOUTS / f"{layout_id}.json"


def read_layout(layout_id: str) -> dict:
    return json.loads(layout_path(layout_id).read_text("utf-8"))


def atomic_write(path: Path, data: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + ".tmp")
    temporary.write_bytes(data)
    os.replace(temporary, path)


def c_string(value: object) -> str:
    text = str(value if value is not None else "")
    return text.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n")


def export_c(layout: dict) -> str:
    view = layout.get("view") or {}
    width = int(view.get("w", 640))
    height = int(view.get("h", 360))
    lines = [
        f"// Auto-generated from {layout.get('id', 'layout')}",
        f"// Logical view: {width} x {height}",
        "// Coordinates are logical pixels before ps_rect() scaling.",
        "",
        "typedef struct { const char *id, *label; int x, y, w, h; } ui_layout_rect_t;",
        "static const ui_layout_rect_t ui_layout[] = {",
    ]
    for item in layout.get("elements", []):
        lines.append(
            '    { "%s", "%s", %d, %d, %d, %d },'
            % (
                c_string(item.get("id")),
                c_string(item.get("label")),
                int(item.get("x", 0)),
                int(item.get("y", 0)),
                int(item.get("w", 0)),
                int(item.get("h", 0)),
            )
        )
    lines += ["};", ""]
    for item in layout.get("elements", []):
        if item.get("type") == "button":
            lines.append(
                'ps_button(fb, %d, %d, %d, %d, "%s", false);'
                % (
                    int(item.get("x", 0)), int(item.get("y", 0)),
                    int(item.get("w", 0)), int(item.get("h", 0)),
                    c_string(item.get("label")),
                )
            )
        elif item.get("type") in ("text", "label"):
            lines.append(
                'ps_text(fb, %d, %d, %d, "%s", %d);'
                % (
                    int(item.get("x", 0)), int(item.get("y", 0)),
                    int(item.get("font", 12)), c_string(item.get("label")),
                    int(item.get("color", 0)),
                )
            )
        elif item.get("type") == "board":
            lines.append(
                "// board: x=%d y=%d w=%d h=%d"
                % (int(item.get("x", 0)), int(item.get("y", 0)),
                   int(item.get("w", 0)), int(item.get("h", 0)))
            )
    lines += ["", "// Drag positions in the editor, then paste these values into the page renderer.", ""]
    return "\n".join(lines)


class Handler(BaseHTTPRequestHandler):
    server_version = "PicoUILayout/1.0"

    def log_message(self, fmt: str, *args: object) -> None:
        print("%s - %s" % (self.address_string(), fmt % args), flush=True)

    def send_bytes(self, status: int, body: bytes, content_type: str) -> None:
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def send_json(self, status: int, payload: object) -> None:
        self.send_bytes(status, json.dumps(payload, ensure_ascii=False).encode("utf-8"),
                        "application/json; charset=utf-8")

    def do_GET(self) -> None:
        path = urlparse(self.path).path
        if path == "/api/health":
            self.send_json(HTTPStatus.OK, {"ok": True, "layouts": str(LAYOUTS)})
            return
        if path == "/api/layouts":
            items = []
            if LAYOUTS.is_dir():
                for file in sorted(LAYOUTS.glob("*.json")):
                    try:
                        data = json.loads(file.read_text("utf-8"))
                        items.append({"id": data.get("id", file.stem),
                                      "name": data.get("name", file.stem),
                                      "count": len(data.get("elements", []))})
                    except (OSError, ValueError):
                        continue
            self.send_json(HTTPStatus.OK, {"items": items})
            return
        if path.startswith("/api/layout/"):
            layout_id = safe_layout_id(path.rsplit("/", 1)[-1])
            if not layout_id or not layout_path(layout_id).is_file():
                self.send_json(HTTPStatus.NOT_FOUND, {"error": "layout not found"})
                return
            try:
                self.send_json(HTTPStatus.OK, read_layout(layout_id))
            except (OSError, ValueError) as exc:
                self.send_json(HTTPStatus.INTERNAL_SERVER_ERROR, {"error": str(exc)})
            return
        if path.startswith("/api/export/"):
            layout_id = safe_layout_id(path.rsplit("/", 1)[-1])
            if not layout_id or not layout_path(layout_id).is_file():
                self.send_json(HTTPStatus.NOT_FOUND, {"error": "layout not found"})
                return
            try:
                self.send_bytes(HTTPStatus.OK, export_c(read_layout(layout_id)).encode("utf-8"),
                                "text/plain; charset=utf-8")
            except (OSError, ValueError) as exc:
                self.send_json(HTTPStatus.INTERNAL_SERVER_ERROR, {"error": str(exc)})
            return
        self.serve_static(path)

    def serve_static(self, path: str) -> None:
        if path == "/":
            path = "/index.html"
        relative = Path(unquote(path).lstrip("/"))
        target = (ROOT / relative).resolve()
        try:
            target.relative_to(ROOT.resolve())
        except ValueError:
            self.send_bytes(HTTPStatus.FORBIDDEN, b"forbidden", "text/plain")
            return
        if not target.is_file():
            self.send_bytes(HTTPStatus.NOT_FOUND, b"not found", "text/plain")
            return
        content_type = mimetypes.guess_type(target.name)[0] or "application/octet-stream"
        if target.suffix == ".js":
            content_type = "text/javascript; charset=utf-8"
        elif target.suffix == ".css":
            content_type = "text/css; charset=utf-8"
        elif target.suffix in (".html", ".htm"):
            content_type = "text/html; charset=utf-8"
        self.send_bytes(HTTPStatus.OK, target.read_bytes(), content_type)

    def do_PUT(self) -> None:
        path = urlparse(self.path).path
        if not path.startswith("/api/layout/"):
            self.send_json(HTTPStatus.NOT_FOUND, {"error": "not found"})
            return
        layout_id = safe_layout_id(path.rsplit("/", 1)[-1])
        if not layout_id:
            self.send_json(HTTPStatus.BAD_REQUEST, {"error": "invalid layout id"})
            return
        try:
            length = int(self.headers.get("Content-Length", "0"))
        except ValueError:
            length = 0
        if length <= 0 or length > MAX_BODY:
            self.send_json(HTTPStatus.BAD_REQUEST, {"error": "invalid body size"})
            return
        try:
            payload = json.loads(self.rfile.read(length).decode("utf-8"))
        except (UnicodeDecodeError, ValueError):
            self.send_json(HTTPStatus.BAD_REQUEST, {"error": "invalid JSON"})
            return
        if not isinstance(payload, dict) or not isinstance(payload.get("elements"), list):
            self.send_json(HTTPStatus.BAD_REQUEST, {"error": "layout needs elements[]"})
            return
        payload["id"] = layout_id
        payload.setdefault("name", layout_id)
        payload.setdefault("view", {"w": 640, "h": 360})
        try:
            atomic_write(layout_path(layout_id),
                         (json.dumps(payload, ensure_ascii=False, indent=2) + "\n").encode("utf-8"))
        except OSError as exc:
            self.send_json(HTTPStatus.INTERNAL_SERVER_ERROR, {"error": str(exc)})
            return
        self.send_json(HTTPStatus.OK, {"ok": True, "id": layout_id})


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, default=8787)
    parser.add_argument("--host", default="127.0.0.1")
    args = parser.parse_args()
    LAYOUTS.mkdir(parents=True, exist_ok=True)
    server = ThreadingHTTPServer((args.host, args.port), Handler)
    print(f"Pico UI layout editor: http://{args.host}:{args.port}", flush=True)
    print(f"Layouts: {LAYOUTS}", flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
