#!/usr/bin/env python3
"""Verify the shelf open-choice prompt and progress retention across rapid switches."""
from pathlib import Path
import argparse
import json
import select
import subprocess
import tempfile
import time

from test_native_preview_book import png_from_pgm


ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument("--binary", type=Path, required=True)
parser.add_argument("--output", type=Path, required=True)
args = parser.parse_args()
out = args.output.resolve()
out.mkdir(parents=True, exist_ok=True)
run = Path(tempfile.mkdtemp(prefix="dual-shelf-choice-", dir=out))
history = []


class Device:
    def __init__(self, name, font):
        self.name = name
        self.root = run / name
        self.books = self.root / "tf/books"
        self.books.mkdir(parents=True)
        for book in ("01-alpha.txt", "02-beta.txt", "03-gamma.txt"):
            (self.books / book).write_text(
                "".join(
                    f"第{i}章 Chapter {i}\n"
                    + ("Shelf choice and rapid progress retention fixture.\n" * 180)
                    for i in range(1, 5)
                )
            )
        self.log = (self.root / "runtime.log").open("w")
        self.p = subprocess.Popen(
            [
                str(args.binary.resolve()),
                str(font),
                str(self.root),
                str(self.root / "frame.pgm"),
            ],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=self.log,
            text=True,
        )
        self.state = {}
        self.now = 10000
        self.cmd("status")
        self.cmd("route 0")

    def cmd(self, line):
        self.p.stdin.write(line + "\n")
        self.p.stdin.flush()
        if not select.select([self.p.stdout], [], [], 30)[0]:
            raise RuntimeError(f"{self.name} timeout: {line}")
        result = self.p.stdout.readline()
        if not result:
            raise RuntimeError(f"{self.name} exited: {self.log.name}")
        self.state = json.loads(result)
        assert self.state["ok"], (line, self.state)
        self.now = self.state["now_ms"]
        history.append({"device": self.name, "command": line, "state": self.state})
        return self.state

    def tick(self):
        self.now += 20
        return self.cmd(f"tick {self.now}")

    def tap(self, x, y):
        self.cmd(f"touch down {x} {y} {self.now + 20}")
        return self.cmd(f"touch up {x} {y} {self.now + 60}")

    def capture(self, name):
        png_from_pgm(self.root / "frame.pgm", run / f"{name}-{self.name}.png")

    def close(self):
        if self.p.poll() is None:
            try:
                self.cmd("quit")
                self.p.wait(timeout=5)
            except Exception:
                self.p.terminate()
                self.p.wait(timeout=5)
        self.log.close()


devices = []
try:
    primary = Device("primary", ROOT / "main/assets/serif.ttf")
    secondary = Device("secondary", ROOT / "main/assets/kai.ttf")
    devices = [primary, secondary]

    def pump(count=1):
        for _ in range(count):
            primary.tick()
            secondary.tick()
            time.sleep(0.001)

    def until(predicate, label, limit=8000):
        for _ in range(limit):
            pump()
            if predicate():
                return
        raise AssertionError((label, primary.state, secondary.state))

    primary.cmd("dual 1 127.0.0.1 123456")
    secondary.cmd("dual 2 127.0.0.1 123456")
    until(lambda: primary.state["dual_synced"] and secondary.state["dual_synced"], "sync")

    primary.cmd("dual-open /sdcard/books/01-alpha.txt")
    until(
        lambda: primary.state.get("active_app") == secondary.state.get("active_app") == "book"
        and primary.state.get("book_path", "").endswith("/01-alpha.txt")
        and secondary.state.get("book_path", "").endswith("/01-alpha.txt"),
        "open alpha together",
    )
    for _ in range(3):
        primary.cmd("key 2 short")
        pump(20)
    previous_page = secondary.state.get("dual_page")
    primary.cmd("key 1 short")
    until(
        lambda: primary.state["route"] == 0
        and primary.state.get("active_app") != "book"
        and primary.state["dual_has_reading"],
        "primary leaves paired reading",
    )

    # Any new shelf selection now asks for local or paired opening.
    primary.tap(160, 401)
    pump(3)
    assert primary.state["shelf_open_choice"], primary.state
    assert primary.state["route"] == 0 and primary.state.get("active_app") != "book", primary.state
    primary.capture("shelf-open-choice")

    # Local choice must not switch the peer.
    primary.tap(180, 279)
    until(
        lambda: primary.state.get("active_app") == "book"
        and primary.state.get("book_path", "").endswith("/02-beta.txt")
        and secondary.state.get("book_path", "").endswith("/01-alpha.txt")
        and secondary.state.get("dual_page") == previous_page,
        "local-only open leaves peer unchanged",
    )
    local_path = primary.state["book_path"]
    for _ in range(4):
        primary.cmd("key 2 short")
        pump(20)
    local_position = (primary.state["book_chapter"], primary.state["book_page"])
    primary.cmd("key 1 short")
    until(lambda: primary.state["route"] == 0 and primary.state.get("active_app") != "book", "local shelf")

    # Alternate the recent-first shelf rows, finishing quickly each time.
    saved = {local_path: local_position}
    last_path = local_path
    for index in range(8):
        primary.tap(160, 401 if index % 2 == 0 else 329)
        pump(2)
        assert primary.state["shelf_open_choice"], (index, primary.state)
        primary.tap(180, 279)
        until(lambda: primary.state.get("active_app") == "book", f"rapid local open {index}")
        path = primary.state["book_path"]
        if path in saved:
            assert (primary.state["book_chapter"], primary.state["book_page"]) >= saved[path], (
                "saved progress regressed",
                path,
                saved[path],
                primary.state,
            )
        primary.cmd("key 2 short")
        pump(3)
        saved[path] = (primary.state["book_chapter"], primary.state["book_page"])
        primary.cmd("key 1 short")
        until(lambda: primary.state["route"] == 0 and primary.state.get("active_app") != "book", f"rapid shelf {index}")
        last_path = path
    assert len(saved) >= 2, saved

    # The explicit dual choice resumes normal paired opening.
    primary.tap(160, 329)
    pump(2)
    assert primary.state["shelf_open_choice"], primary.state
    primary.tap(180, 393)
    until(
        lambda: primary.state.get("active_app") == secondary.state.get("active_app") == "book"
        and primary.state.get("book_path") == secondary.state.get("book_path")
        and primary.state.get("dual_target") == secondary.state.get("dual_target") == 0,
        "paired choice opens both devices",
    )

    (run / "report.json").write_text(
        json.dumps(
            {
                "passed": True,
                "physical_devices_tested": False,
                "checks": [
                    "shelf new-book prompt appears only after leaving paired reading",
                    "local choice leaves the peer on its current paired page",
                    "rapid alternating shelf switches retain each non-zero book position",
                    "dual choice opens the same book on both devices",
                ],
                "saved_positions": saved,
                "last_path": last_path,
                "primary": primary.state,
                "secondary": secondary.state,
            },
            ensure_ascii=False,
            indent=2,
        )
        + "\n"
    )
    print("PASS: shelf choice prompt, local/paired opening and rapid-switch progress retention")
finally:
    (run / "transcript.json").write_text(json.dumps(history, ensure_ascii=False, indent=2) + "\n")
    for device in devices:
        try:
            device.close()
        except Exception:
            pass
    print(run)
