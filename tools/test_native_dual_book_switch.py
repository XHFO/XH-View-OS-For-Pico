#!/usr/bin/env python3
"""Regression: a secondary shelf selection resumes an older book without clearing progress."""
from pathlib import Path
import argparse, json, select, subprocess, tempfile, time

ROOT = Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser()
p.add_argument("--binary", type=Path, required=True)
p.add_argument("--output", type=Path, required=True)
a = p.parse_args()
out = a.output.resolve()
out.mkdir(parents=True, exist_ok=True)
run = Path(tempfile.mkdtemp(prefix="dual-book-switch-", dir=out))
history = []


class Device:
    def __init__(self, name, font, books=()):
        self.name = name
        self.root = run / name
        root = self.root / "tf/books"
        root.mkdir(parents=True, exist_ok=True)
        for path, text in books:
            (root / path).write_text(text)
        self.log = (self.root / "runtime.log").open("w")
        self.p = subprocess.Popen(
            [str(a.binary.resolve()), str(font), str(self.root), str(self.root / "frame.pgm")],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=self.log, text=True
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

    def close(self):
        if self.p.poll() is None:
            try:
                self.cmd("quit")
                self.p.wait(timeout=5)
            except Exception:
                self.p.terminate()
                self.p.wait(timeout=5)
        self.log.close()


long_text = "".join(
    f"第{i}章 Chapter {i}\n"
    + ("Previous reading position must survive a paired book switch.\n" * 180)
    for i in range(1, 5)
)
history_text = long_text.replace(
    "Previous reading position must survive a paired book switch.",
    "Older history content has a distinct digest and saved position.",
)
devices = []
active_devices = []
try:
    seed = Device("primary", ROOT / "main/assets/serif.ttf", (
        ("a-current.txt", long_text), ("b-history.txt", history_text),
    ))
    devices = [seed]
    active_devices[:] = [seed]

    def pump(count=1):
        for _ in range(count):
            for device in active_devices:
                device.tick()
            time.sleep(0.001)

    def until(predicate, label, limit=8000):
        for _ in range(limit):
            pump()
            if predicate():
                return
        raise AssertionError((label, [device.state for device in active_devices]))

    # Seed progress on b-history before pairing.
    pump(80)
    seed.tap(60, 410)
    until(lambda: seed.state.get("active_app") == "book"
          and seed.state.get("book_path", "").endswith("/b-history.txt"),
          "open older book in standalone mode")
    for _ in range(8):
        seed.cmd("powerkey")
        pump(30)
    saved_position = (seed.state.get("book_chapter", 0), seed.state.get("book_page", 0))
    assert saved_position > (0, 1), seed.state
    seed.close()
    devices.remove(seed)
    active_devices.clear()

    primary = Device("primary", ROOT / "main/assets/serif.ttf")
    secondary = Device("secondary", ROOT / "main/assets/kai.ttf", (
        ("a-current.txt", long_text), ("b-history.txt", history_text),
    ))
    devices = [primary, secondary]
    active_devices[:] = [primary, secondary]

    primary.cmd("dual 1 127.0.0.1 123456")
    secondary.cmd("dual 2 127.0.0.1 123456")
    until(lambda: primary.state["dual_synced"] and secondary.state["dual_synced"],
          "initial paired sync")
    pump(40)
    secondary.cmd("scenario offline")
    secondary.cmd("dual-peer 127.0.0.1 1 PicoDual-STALE")
    pump(40)
    assert secondary.state["services"]["wifiEnabled"], secondary.state
    assert not secondary.state["services"]["connected"], secondary.state
    secondary.cmd("scenario default")
    pump(10)

    # Start a different current book, then make the secondary choose the older one.
    primary.cmd("dual-open /sdcard/books/a-current.txt")
    until(lambda: primary.state.get("active_app") == secondary.state.get("active_app") == "book"
          and primary.state.get("book_path", "").endswith("/a-current.txt")
          and primary.state.get("dual_target") == secondary.state.get("dual_target") == 0,
          "open current paired book")
    secondary.cmd("book-select /sdcard/books/b-history.txt")
    until(lambda: primary.state.get("active_app") == secondary.state.get("active_app") == "book"
          and primary.state.get("book_path", "").endswith("/b-history.txt")
          and secondary.state.get("book_path", "").endswith("/b-history.txt")
          and primary.state.get("dual_target") == secondary.state.get("dual_target") == 0,
          "secondary selects previously read book")
    opened_position = (primary.state.get("book_chapter", 0), primary.state.get("book_page", 0))
    assert opened_position >= saved_position, (saved_position, primary.state, secondary.state)

    (run / "report.json").write_text(json.dumps({
        "passed": True,
        "physical_devices_tested": False,
        "saved_position": saved_position,
        "primary": primary.state,
        "secondary": secondary.state,
    }, ensure_ascii=False, indent=2) + "\n")
    print("PASS: secondary switched to a previously read paired book at its saved position")
finally:
    (run / "transcript.json").write_text(json.dumps(history, ensure_ascii=False, indent=2) + "\n")
    for device in devices:
        try:
            device.close()
        except Exception:
            pass
    print(run)
