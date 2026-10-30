#!/usr/bin/env python3
"""Render the shipped HA Jinja template and verify bounded firmware consumption.
Requires Jinja2 3.1.x (test dependency only). No production HA account is used.
"""
from pathlib import Path
import json
import math
import os
import subprocess
import tempfile
from types import SimpleNamespace
from jinja2 import StrictUndefined
from jinja2.sandbox import ImmutableSandboxedEnvironment

ROOT = Path(__file__).resolve().parents[1]
class States:
    def __init__(self, rows):
        self.rows = rows
        self.by_id = {x.entity_id: x for x in rows}
    def __iter__(self):
        return iter(self.rows)
    def __getitem__(self, name):
        return self.by_id.get(name)

def state(name, value="on", **attrs):
    return SimpleNamespace(entity_id=name, domain=name.partition(".")[0], state=value, attributes=attrs)

def main():
    with tempfile.TemporaryDirectory(prefix="pico-ha-projection-") as directory:
        path = Path(directory); binary = path / "projection"
        command = [os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra", "-Werror", "-g", "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                        "-I", str(ROOT / "components/read_pico_home_link/include"), "-I", str(ROOT / "managed_components/espressif__cjson/cJSON"),
                        str(ROOT / "tools/home_link_projection_host_test.c"), str(ROOT / "components/read_pico_home_link/home_link_json.c"), "-lm", "-o", str(binary)]
        subprocess.run(command, check=True)
        request = json.loads(subprocess.check_output([str(binary), "template"]))
        env = ImmutableSandboxedEnvironment(undefined=StrictUndefined)
        # HA exposes to_json; controls exercise the largest six-byte escaped representation.
        env.filters["to_json"] = lambda value, ensure_ascii=False: json.dumps(value, ensure_ascii=ensure_ascii, allow_nan=False)
        def is_number(value):
            try: return math.isfinite(float(value))
            except (ValueError, TypeError): return False
        env.globals["is_number"] = is_number
        template = env.from_string(request["template"])
        def render(rows, printers=None):
            return template.render(states=States(rows), printers=printers if printers is not None else request["variables"]["printers"], area_name=lambda entity: "书房" if entity == "light.first" else None)
        huge = "x" * 300000
        rows = [state("light.first", friendly_name="阅读灯", huge_unused=huge, temperature="NaN")]
        rows += [state("sensor.noise_"+str(i), "1", device_class="power", history=huge) for i in range(600)]
        rows += [state("light.light_"+str(i), friendly_name="灯"+str(i), media=huge) for i in range(999)]
        rows += [state("sensor.selected_print_status", "running", friendly_name="Selected printer"), state("sensor.selected_print_progress", "73"), state("sensor.selected_task_name", "Keep this task")]
        rendered = render(rows); payload = json.loads(rendered)
        assert len(rendered.encode()) < 192*1024
        assert payload[-1] == {"pico_total":1000, "pico_cut":False}
        assert len([e for e in payload if e.get("pico_printer")]) == 3
        assert not any("huge_unused" in e.get("attributes", {}) for e in payload)
        fixture = path / "large.json"; fixture.write_text(rendered)
        subprocess.run([str(binary), str(fixture), "96", "1000"], check=True)
        controls=[state("light.first",friendly_name="灯"), state("switch.socket","off",friendly_name=None,unit_of_measurement=None,room=None), state("climate.ac","cool",temperature=26.5), state("fan.ceiling","unavailable"), state("sensor.temp","23.5",device_class="temperature"), state("sensor.humidity","48",device_class="humidity")]
        small=path/"six.json"; small.write_text(render(controls+rows[-3:]))
        nulls=json.loads(small.read_text())[1]["attributes"]; assert nulls["friendly_name"]==nulls["unit_of_measurement"]==nulls["area_name"]==""
        subprocess.run([str(binary),str(small),"6","6"],check=True)
        empty = path / "empty.json"; empty.write_text(render([]))
        subprocess.run([str(binary), str(empty), "0", "0"], check=True)
        # Maximum field widths with JSON escaping, all 8 printer prefixes, both naming variants.
        bad = "\x01" * 1000
        worst = [state("light."+str(i).zfill(89), bad, friendly_name=bad, room=bad, unit_of_measurement=bad, device_class=bad) for i in range(96)]
        suffixes=["_print_status","_printing_state","_print_progress","_progress","_remaining_time","_current_layer","_total_layer_count","_total_layers","_nozzle_temperature","_bed_temperature","_task_name","_subtask_name"]
        prefixes=["sensor."+str(i).zfill(60) for i in range(8)]
        for prefix in prefixes:
            worst.extend(state(prefix+suffix, bad, friendly_name=bad) for suffix in suffixes)
        worst_json=render(worst, prefixes[:3]); assert len(worst_json.encode())<192*1024, len(worst_json.encode())
        too_long=state("light."+"x"*96); assert json.loads(render([too_long]))[-1]["pico_total"]==0
        discovered=json.loads(render([state("sensor.p"+str(i)+"_print_status", "idle") for i in range(20)], []))
        assert discovered[-1]["pico_cut"] and len(discovered)==9
        subprocess.run(command+["-DPICO_HOME_LINK_FULL=0"],check=True)
        subprocess.run([str(binary),str(small),"6","6"],check=True)
        print(f"Jinja: 1603 entities / huge individual attributes projected to {len(rendered.encode())} bytes; adversarial max {len(worst_json.encode())} / {192*1024} bytes; empty/overflow/long IDs passed")
if __name__ == "__main__": main()
