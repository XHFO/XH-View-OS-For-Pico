#!/usr/bin/env python3
"""中文：实际应用集触摸、TF模块退出、旧导航与Cloudflare空态集成；仅私有TF。/ English: Real Apps touch, TF-module return, saved navigation and Cloudflare empty state in private TF sessions."""
from pathlib import Path
import argparse
import hashlib
import json
import os

from test_native_preview_shelf_ble import ROOT, Runtime
from test_native_desktop import bitmap


def card_center(index, landscape):
    cols, rows, width, height = (3, 2, 640, 360) if landscape else (2, 3, 360, 640)
    top, gap = (121 if landscape else 148), 10
    card_width = (width - 32 - (cols - 1) * gap) // cols
    card_height = (height - top - 77 - (rows - 1) * gap) // rows
    return (16 + index % cols * (card_width + gap) + card_width // 2,
            top + index // cols * (card_height + gap) + card_height // 2)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    binary, output = args.binary.resolve(), args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    os.environ["RPICO_HOME_LINK_OFFLINE"] = "1"
    report = {"result": "RUNNING", "hardware_access": False, "actual_tf_modules": True,
              "binary_sha256": hashlib.sha256(binary.read_bytes()).hexdigest(), "checks": [], "scenes": {}}
    try:
        for landscape in (False, True):
            label = "landscape" if landscape else "portrait"
            session = output / ("session-" + label)
            session.mkdir()
            runtime = Runtime(binary, ROOT / "sdcard/fonts/MiSansVF.ttf", session, output)
            width, height = (640, 360) if landscape else (360, 640)
            try:
                state = runtime.start()
                assert state["games_count"] == 6, state
                canvas = session / "tf/readpico/desktop/canvas.bmp"
                canvas.parent.mkdir(parents=True, exist_ok=True)
                canvas.write_bytes(bitmap())
                runtime.command("rotate " + str(int(landscape)))
                # 新配置默认第四页应用集；实际点击顶栏而非直接注入路由。/ New configurations expose Apps as the fourth Home page; enter through the actual tab.
                cell_width = (width - 72 - 12) // 4
                state = runtime.tap(12 + 3 * (cell_width + 4) + cell_width // 2, 48)
                assert state["route"] == 33 and state["route_name"] == "应用集", state
                runtime.capture("apps-home-" + label, state)
                report["checks"].append(label + "-default-apps-tab")
                state = runtime.tap(*card_center(5, landscape))
                assert state["route"] == 32 and state["route_name"] == "网站流量", state
                runtime.capture("cloudflare-empty-" + label, state)
                assert runtime.tap(23, 47)["route"] == 33
                state = runtime.tap(*card_center(4, landscape))
                assert state["route"] == 29, state
                runtime.capture("ordinary-board-" + label, state)
                assert runtime.command("key 1 click")["route"] == 33
                report["checks"].append(label + "-independent-cloudflare-and-board")
                # 已注册工具位于第二页第一格，六游戏紧随；每个模块均通过真实包载入。/ The registered tool precedes six real package-loaded games on subsequent pages.
                runtime.tap(width - 50, height - 50)
                state = runtime.tap(*card_center(0, landscape))
                assert state["route"] == 28 and not state["games_error"], state
                runtime.capture("drawing-tool-" + label, state)
                assert runtime.command("key 1 click")["route"] == 33
                for game in range(6):
                    if game == 5:
                        runtime.tap(width - 50, height - 50)
                    state = runtime.tap(*card_center(game + 1 if game < 5 else 0, landscape))
                    assert state["route"] == (14 if game < 4 else 23) and not state["games_error"], (game, state)
                    runtime.capture(f"game-{game}-from-apps-{label}", state)
                    if game < 4:
                        state = runtime.command("key 1 click")
                    else:
                        # 休闲模块内的“返回列表”经过PGA_PAGE_LAUNCHER与真实finish_call。/ Arcade's own return button exercises PGA_PAGE_LAUNCHER and the real finish_call.
                        state = runtime.tap(517 if landscape else 241, 238 if landscape else 528)
                    assert state["route"] == 33, (game, state)
                    report["checks"].append(f"{label}-module-{game}-returns-to-apps")
                runtime.capture("apps-last-page-" + label, state)
                if not landscape:
                    for game_id in ("gomoku", "go", "xiangqi", "chess", "2048", "snake"):
                        runtime.command("game-remove " + game_id)
                    state = runtime.command("status")
                    assert state["games_count"] == 0 and state["route"] == 33
                    runtime.capture("apps-after-removing-games", state)
                    assert runtime.tap(*card_center(1, landscape))["route"] == 5
                    assert runtime.command("key 1 click")["route"] == 33
                    report["checks"].append("background-catalog-shrink-keeps-valid-paint-and-hit")
                    # 用真实编排隐藏应用集并交换首页前两项，既有序列不被应用集改写。/ Hide Apps and swap the first two Home entries through the actual editor.
                    runtime.command("route 27")
                    runtime.tap(80, 152 + 3 * 57 + 20)
                    runtime.tap(290, 472)
                    runtime.tap(80, 152 + 57 + 20)
                    runtime.tap(45, 472)
                    state = runtime.tap(width - 75, height - 57)
                    assert state["route"] == 33
                    runtime.command("key 0 click")
                    runtime.capture("apps-custom-home-order", runtime.command("status"))
                    assert runtime.tap(*card_center(0, landscape))["route"] == 3
                    assert runtime.command("key 1 click")["route"] == 33
                    assert runtime.tap(23, 47)["route"] == 3
                    runtime.command("route 0")
                    runtime.command("route 5")
                    # 新设置按用途分组；隐藏的应用集仍可通过“应用与服务”访问。
                    # Grouped settings retain access to Apps even after its Home tab is hidden.
                    state = runtime.tap(width // 2, 158 + 4 * 60 + 30)
                    assert state["route"] == 5, state
                    state = runtime.tap(width // 2, 126 + 76 // 2)
                    assert state["route"] == 33, state
                    runtime.capture("apps-hidden-but-settings-accessible", state)
                    assert runtime.tap(23, 47)["route"] == 5
                    report["checks"].append("saved-home-order-hidden-apps-and-fixed-settings-entry")
                report["scenes"].update(runtime.scenes)
            finally:
                runtime.close()
                (session / "transcript.json").write_text(json.dumps(runtime.transcript, ensure_ascii=False, indent=2) + "\n")
        report["result"] = "PASS"
    finally:
        (output / "report.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n")
    print(json.dumps({"result": report["result"], "checks": len(report["checks"]), "scenes": len(report["scenes"])}))


if __name__ == "__main__":
    main()
