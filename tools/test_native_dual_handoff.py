#!/usr/bin/env python3
"""Verify atomic dual-reader handoff after either role reads alone."""
from pathlib import Path
import argparse, json, select, subprocess, tempfile, time
from test_native_preview_book import png_from_pgm

ROOT = Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser()
p.add_argument("--binary", type=Path, required=True)
p.add_argument("--output", type=Path, required=True)
a = p.parse_args()
out = a.output.resolve()
out.mkdir(parents=True, exist_ok=True)
run = Path(tempfile.mkdtemp(prefix="dual-handoff-", dir=out))
history = []


class Device:
    def __init__(self, name, font):
        self.name = name
        self.root = run / name
        self.books = self.root / "tf/books"
        self.books.mkdir(parents=True)
        self.now = 10000
        (self.books / f"{name}.txt").write_text(
            "".join(
                f"第{i}章 Chapter {i}\n"
                + (f"{name} handoff text. Stable paired pagination and progress relay.\n" * 180)
                for i in range(1, 5)
            )
        )
        self.log = (self.root / "runtime.log").open("w")
        self.p = subprocess.Popen(
            [str(a.binary.resolve()), str(font), str(self.root), str(self.root / "frame.pgm")],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=self.log,
            text=True,
        )
        self.state = {}
        self.cmd("status")
        self.cmd("route 34")

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
    primary.cmd("dual 1 127.0.0.1 123456")
    secondary.cmd("dual 2 127.0.0.1 123456")

    def pump(count=1):
        for _ in range(count):
            primary.tick()
            secondary.tick()
            time.sleep(0.001)

    def until(predicate, label, limit=6000):
        for _ in range(limit):
            pump()
            if predicate():
                return
        raise AssertionError((label, primary.state, secondary.state))

    def first_visible_after(device, start):
        for item in history[start:]:
            state = item["state"]
            if (item["device"] == device and state.get("active_app") == "book"
                    and state.get("book_view") == "reading"
                    and state.get("book_pages", 0) and state.get("dual_page", 0)):
                return state["dual_page"]
        return None

    until(
        lambda: primary.state["dual_synced"]
        and secondary.state["dual_synced"]
        and primary.state["route"] == secondary.state["route"] == 0,
        "initial sync and shelf return",
    )
    primary.cmd("dual-usb RPICO DUAL OPEN 0")
    until(
        lambda: primary.state.get("active_app") == secondary.state.get("active_app") == "book"
        and primary.state.get("dual_page") == 1
        and secondary.state.get("dual_page") == 2
        and primary.state.get("dual_target") == secondary.state.get("dual_target") == 0,
        "paired first spread",
    )

    # Primary leaves while the secondary keeps reading and relays each solo page.
    primary.cmd("key 1 short")
    until(
        lambda: primary.state.get("active_app") != "book"
        and secondary.state.get("book_solo")
        and primary.state["dual_has_reading"],
        "secondary solo after primary shelf",
    )
    primary.capture("primary-return-card")
    secondary.cmd("powerkey")
    until(
        lambda: secondary.state.get("book_page") == 3
        and primary.state["dual_spread"] == 2,
        "secondary solo page relayed while primary on shelf",
    )

    mark = len(history)
    primary.tap(180, 185)
    until(
        lambda: primary.state.get("active_app") == secondary.state.get("active_app") == "book"
        and primary.state.get("dual_page") == 3
        and secondary.state.get("dual_page") == 4
        and not primary.state.get("book_solo")
        and not secondary.state.get("book_solo"),
        "primary atomic return to paired reading",
    )
    assert first_visible_after("primary", mark) == 3, history[mark:]
    primary.capture("primary-returned")
    secondary.capture("secondary-rejoined")

    # Secondary leaves; the primary remains authoritative and advances alone.
    secondary.cmd("key 1 short")
    until(
        lambda: secondary.state.get("active_app") != "book"
        and primary.state.get("book_solo")
        and secondary.state["dual_has_reading"],
        "primary solo after secondary shelf",
    )
    secondary.capture("secondary-return-card")
    primary.tap(330, 300)
    until(lambda: primary.state.get("book_page") == 4, "primary solo first page")
    primary.tap(330, 300)
    until(
        lambda: primary.state.get("book_page") == 5
        and secondary.state["dual_spread"] == 4,
        "primary solo page stored as authoritative spread",
    )

    mark = len(history)
    secondary.tap(180, 185)
    until(
        lambda: secondary.state.get("active_app") == primary.state.get("active_app") == "book"
        and secondary.state.get("book_painted_page") == 6
        and primary.state.get("book_painted_page") == 5
        and not primary.state.get("book_solo")
        and not secondary.state.get("book_solo"),
        "secondary atomic return to paired reading",
    )
    assert first_visible_after("secondary", mark) == 6, history[mark:]
    primary.capture("primary-second-handoff")
    secondary.capture("secondary-second-handoff")

    (run / "report.json").write_text(
        json.dumps(
            {
                "passed": True,
                "physical_devices_tested": False,
                "checks": [
                    "primary shelf exposes shared return card",
                    "secondary solo page relays while primary is away",
                    "primary resumes at latest authoritative spread without an old-page flash",
                    "primary solo pages update the shared authority",
                    "secondary resumes at the authoritative right page without an old-page flash",
                ],
                "last_primary": primary.state,
                "last_secondary": secondary.state,
            },
            ensure_ascii=False,
            indent=2,
        )
        + "\n"
    )
    print("PASS: bidirectional solo handoff, both shelf cards, atomic resume at latest spread")
finally:
    (run / "transcript.json").write_text(json.dumps(history, ensure_ascii=False, indent=2) + "\n")
    for device in devices:
        try:
            device.close()
        except Exception:
            pass
    print(run)
