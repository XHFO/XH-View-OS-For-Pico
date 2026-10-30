#!/usr/bin/env python3
"""Verify branded boot pixels through the production C decoder and macOS Vision.

No devices are opened. The real EPD rotation/readback functions reconstruct the
upright boot screen; OCR and image bounds are independent of generator metadata.
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import shlex
import shutil
import subprocess
import sys
import tempfile

from PIL import Image, ImageChops

ROOT = Path(__file__).resolve().parents[1]
LOCK_HASHES = {
    "lock_4bpp.bin": "436d0e91dc2279246e0d5f7f91f668ca4d22e825ab6476f4ac15d70ffb2a12d9",
    "lock_4bpp.p4b": "b80a627e40f995a0c3eb02a8ad16ed2a6407966433881658668a0afcec1066f6",
}


def function(source, name):
    match = re.search(r"^(?:static )?[A-Za-z_][A-Za-z_0-9 *]*\b" + re.escape(name) + r"\(", source, re.M)
    assert match, name
    begin = source.index("{", match.start())
    depth, end = 1, begin + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[match.start():end]


C_PRELUDE = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ui_image.h"
typedef struct {uint16_t x,y;} Coord_xy;
static int epd_width(void){return 1216;}
static int epd_height(void){return 684;}
#define _swap_int(a,b) do {uint16_t temporary=(a);(a)=(b);(b)=temporary;} while(0)
'''
C_TEST = r'''
int main(int argc,char **argv){
    assert(argc==3);
    FILE *input=fopen(argv[1],"rb");assert(input&&!fseek(input,0,SEEK_END));
    long length=ftell(input);assert(length>=24&&length<=22828);rewind(input);
    uint8_t *packed=malloc((size_t)length),*guard=malloc(415872+32);assert(packed&&guard);
    assert(fread(packed,1,(size_t)length,input)==(size_t)length);assert(!fclose(input));
    memset(guard,0xa5,415872+32);uint8_t *fb=guard+16;
    assert(ui_image_validate(packed,(size_t)length,1216,684));
    assert(ui_image_decode(fb,415872,packed,(size_t)length,1216,684,UI_IMAGE_INVERTED_PORTRAIT));
    for(int i=0;i<16;i++)assert(guard[i]==0xa5&&guard[415872+16+i]==0xa5);
    assert(epd_rotated_display_width()==684&&epd_rotated_display_height()==1216);
    FILE *output=fopen(argv[2],"wb");assert(output);
    assert(fprintf(output,"P5\n684 1216\n255\n")>0);
    for(int y=0;y<1216;y++)for(int x=0;x<684;x++){
        Coord_xy native=_rotate((uint16_t)x,(uint16_t)y);
        assert(native.x<1216&&native.y<684);
        uint8_t pixel=epd_get_pixel(native.x,native.y,1216,684,fb);
        assert(!(pixel&15));assert(fputc((pixel>>4)*17,output)!=EOF);
    }
    assert(!fclose(output));free(guard);free(packed);
    puts("PASS actual C boot decode and legacy EPD portrait readback; framebuffer guards intact");
    return 0;
}
'''
SWIFT_OCR = r'''
import Foundation
import Vision
let request = VNRecognizeTextRequest()
request.recognitionLevel = .accurate
request.recognitionLanguages = ["zh-Hans", "en-US"]
request.usesLanguageCorrection = false
let handler = VNImageRequestHandler(url: URL(fileURLWithPath: CommandLine.arguments[1]), options: [:])
try handler.perform([request])
let rows: [[String: Any]] = (request.results ?? []).compactMap { observation in
    guard let text = observation.topCandidates(1).first else { return nil }
    let b = observation.boundingBox
    return ["text": text.string, "confidence": text.confidence,
            "box": [b.origin.x, 1 - b.origin.y - b.height, b.width, b.height]]
}
let data = try JSONSerialization.data(withJSONObject: rows, options: [.prettyPrinted, .sortedKeys])
FileHandle.standardOutput.write(data)
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--asset-dir", type=Path, default=ROOT / "main/assets")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    assets = args.asset_dir.resolve()
    system = (ROOT / "main/system/pico_system.c").read_text()
    identity = {name: re.search(r'^#define\s+' + macro + r'\s+"([^"\n]+)"\s*$', system, re.M).group(1)
                for name, macro in (("os_name", "PS_OS_NAME"), ("version", "PS_VERSION"), ("release_label", "PS_RELEASE_LABEL"))}
    assert "epd_set_rotation(EPD_ROT_INVERTED_PORTRAIT);" in (ROOT / "components/read_pico/read_pico_init.c").read_text()
    assert "(ui_image_rotation_t)epd_get_rotation()" in (ROOT / "main/app_main.c").read_text()
    for name, expected in LOCK_HASHES.items():
        assert hashlib.sha256((ROOT / "main/assets" / name).read_bytes()).hexdigest() == expected, name

    with tempfile.TemporaryDirectory(prefix="pico-os-boot-") as temporary:
        output = args.output.resolve() if args.output else Path(temporary)
        output.mkdir(parents=True, exist_ok=True)
        state = lambda: {name: (hashlib.sha256((assets / name).read_bytes()).hexdigest(), (assets / name).stat().st_mtime_ns)
                         for name in ("loading_4bpp.bin", "loading_4bpp.p4b")}
        before = state()
        command = [sys.executable, str(ROOT / "tools/generate_os_boot_image.py"), "--asset-dir", str(assets),
                   "--preview", str(output / "forbidden-preview.png"), "--report", str(output / "forbidden-report.json"), "--check"]
        generated = [subprocess.run(command, check=True, text=True, stdout=subprocess.PIPE).stdout for _ in range(2)]
        assert generated[0] == generated[1] and before == state(), "--check must be deterministic and read-only"
        assert not (output / "forbidden-preview.png").exists() and not (output / "forbidden-report.json").exists()
        generator_report = json.loads(generated[0])
        assert generator_report["identity"] == identity
        assert 16 < generator_report["outer_deflate_bytes"] <= 6747

        legacy = (ROOT / "components/epdiy/src/epdiy.c").read_text()
        header = (ROOT / "components/epdiy/src/epdiy.h").read_text()
        rotation = re.search(r"enum EpdRotation\s*\{[^}]*\};", header).group()
        selected = ["epd_rotated_display_width", "epd_rotated_display_height", "_rotate", "epd_get_pixel"]
        native = C_PRELUDE + rotation + "\nstatic enum EpdRotation display_rotation=EPD_ROT_INVERTED_PORTRAIT;\n"
        native += "\n".join(function(legacy, name) for name in selected) + C_TEST
        (output / "decode.c").write_text(native)
        compiler = shlex.split(os.environ.get("CC", "cc"))
        compile_command = compiler + ["-std=c11", "-O1", "-g", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
                                      "-fno-omit-frame-pointer", "-I", str(ROOT / "main/ui"), str(output / "decode.c"),
                                      str(ROOT / "main/ui/ui_image.c"), "-o", str(output / "decode")]
        subprocess.run(compile_command, check=True)
        subprocess.run([str(output / "decode"), str(assets / "loading_4bpp.p4b"), str(output / "decoded.pgm")], check=True)
        with Image.open(output / "decoded.pgm") as decoded:
            image = decoded.copy()
        assert image.mode == "L" and image.size == (684, 1216)
        gray_levels = sorted(set(image.tobytes()))
        assert 3 <= len(gray_levels) <= 16 and all(value % 17 == 0 for value in gray_levels)
        bounds = ImageChops.difference(image, Image.new("L", image.size, 255)).getbbox()
        assert bounds and bounds[0] >= 40 and bounds[1] >= 40 and bounds[2] <= 644 and bounds[3] <= 1176
        image.save(output / "decoded.png")

        swift = shutil.which("swiftc")
        assert swift, "macOS Swift/Vision is required for independent boot OCR"
        (output / "ocr.swift").write_text(SWIFT_OCR)
        subprocess.run([swift, "-module-cache-path", str(output / "swift-cache"), str(output / "ocr.swift"), "-o", str(output / "ocr")], check=True)
        result = subprocess.run([str(output / "ocr"), str(output / "decoded.png")], check=True, text=True, stdout=subprocess.PIPE)
        rows = sorted(json.loads(result.stdout), key=lambda row: (row["box"][1], row["box"][0]))
        (output / "ocr.json").write_text(json.dumps(rows, ensure_ascii=False, indent=2) + "\n")
        normalize = lambda text: "".join(text.split())
        text = normalize(" ".join(row["text"] for row in rows))
        expected_text = [identity["os_name"], identity["release_label"], identity["version"], "正在启动", "Starting up"]
        positions = []
        for expected in expected_text:
            assert normalize(expected) in text, (expected, rows)
            positions.append(text.index(normalize(expected)))
        assert positions == sorted(positions), "upright boot text must follow brand/channel/version/startup order"
        for row in rows:
            x, y, width, height = row["box"]
            assert 0 < x < x + width < 1 and 0 < y < y + height < 1 and height > 0
        report = {"result": "PASS", "hardware_access": False, "sanitizers": ["address", "undefined"],
                  "identity": identity, "logical_size": list(image.size), "gray_levels": gray_levels, "pixel_bounds": list(bounds),
                  "raw_sha256": before["loading_4bpp.bin"][0], "p4b_sha256": before["loading_4bpp.p4b"][0],
                  "decoded_pixels_sha256": hashlib.sha256(image.tobytes()).hexdigest(), "ocr": rows,
                  "legacy_lock_hashes_unchanged": LOCK_HASHES, "deterministic_check_read_only": True}
        (output / "report.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n")
        print("PASS branded boot: deterministic/read-only generator; actual C decode + legacy EPD portrait mapping; native 16-gray pixels; exact brand/version/channel OCR; bounded text; legacy lock unchanged")


if __name__ == "__main__":
    main()
