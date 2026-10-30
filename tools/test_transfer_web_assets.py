#!/usr/bin/env python3
"""Validate lossless page archives, fail-closed freshness, and real upload HTTP gzip response."""
import gzip
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

from test_pico_transfer_bridge import function

ROOT = Path(__file__).resolve().parents[1]
COMPONENT = ROOT / "components/read_pico_transfer"


def invoke(source: Path, output: Path, archive: Path) -> subprocess.CompletedProcess:
    return subprocess.run([sys.executable, str(COMPONENT / "compress_management.py"), str(source), str(output),
                           "--archive", str(archive)], cwd=ROOT, text=True, capture_output=True)


def main() -> None:
    pages = []
    with tempfile.TemporaryDirectory(prefix="pico-transfer-web-assets-") as temporary:
        temp = Path(temporary)
        for name in ("manage", "upload"):
            source = COMPONENT / (name + ".html")
            archive = COMPONENT / "assets" / (name + ".html.gz")
            raw, encoded = source.read_bytes(), archive.read_bytes()
            assert gzip.decompress(encoded) == raw
            raw.decode("utf-8", errors="strict")
            assert encoded[:8] == bytes.fromhex("1f8b080000000000")
            output = temp / (name + ".html.gz")
            result = invoke(source, output, archive)
            assert result.returncode == 0, result.stderr
            assert output.read_bytes() == encoded
            # Every rejection must leave the output untouched; no larger fallback may be built.
            sentinel = b"output-must-not-be-replaced"
            output.write_bytes(sentinel)
            stale = temp / (name + ".html")
            stale.write_bytes(raw + b"\n<!-- new source -->")
            assert invoke(stale, output, archive).returncode != 0
            assert output.read_bytes() == sentinel
            assert invoke(source, output, temp / "missing.gz").returncode != 0
            assert output.read_bytes() == sentinel
            corrupt = temp / (name + "-corrupt.gz")
            corrupt.write_bytes(encoded[:-5])
            assert invoke(source, output, corrupt).returncode != 0
            assert output.read_bytes() == sentinel
            previous = len(gzip.compress(raw, compresslevel=9, mtime=0))
            pages.append({"name": name, "source_bytes": len(raw), "gzip_bytes": len(encoded),
                          "previous_gzip9_bytes": previous, "saved_bytes": previous - len(encoded),
                          "sha256": hashlib.sha256(encoded).hexdigest()})

        # Run the unchanged production HTTP handler with the same validated archive bytes.
        handler = function((COMPONENT / "read_pico_transfer.c").read_text(), "index_handler")
        archive = (COMPONENT / "assets/upload.html.gz").read_bytes()
        c = temp / "upload-page.c"
        c.write_text(r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
typedef int esp_err_t;
typedef struct { bool gzip, utf8; const char *body; size_t bytes; } httpd_req_t;
static const unsigned char upload_asset[] = {''' + ",".join(str(byte) for byte in archive) + r'''};
#define upload_start ((const char *)upload_asset)
#define upload_end ((const char *)upload_asset + sizeof(upload_asset))
static esp_err_t httpd_resp_set_type(httpd_req_t *r, const char *s) {
    assert(!strcmp(s, "text/html; charset=utf-8")); r->utf8 = true; return 0;
}
static esp_err_t httpd_resp_set_hdr(httpd_req_t *r, const char *key, const char *value) {
    assert(!strcmp(key, "Content-Encoding") && !strcmp(value, "gzip")); r->gzip = true; return 0;
}
static esp_err_t httpd_resp_send(httpd_req_t *r, const char *body, ssize_t bytes) {
    assert(bytes >= 0); r->body = body; r->bytes = (size_t)bytes; return 0;
}
''' + handler + r'''
int main(void) {
    httpd_req_t request = {0};
    assert(!index_handler(&request) && request.gzip && request.utf8);
    assert(request.bytes == sizeof(upload_asset) && !memcmp(request.body, upload_asset, request.bytes));
    assert(fwrite(request.body, 1, request.bytes, stdout) == request.bytes);
    return 0;
}
''')
        binary = temp / "upload-page"
        subprocess.run([os.environ.get("CC", "cc"), "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-g",
                        "-fsanitize=address,undefined", "-fno-omit-frame-pointer", str(c), "-o", str(binary)],
                       cwd=ROOT, check=True)
        response = subprocess.check_output([str(binary)], cwd=ROOT)
        assert response == archive
        assert gzip.decompress(response) == (COMPONENT / "upload.html").read_bytes()
    report = {"passed": True, "pages": pages, "total_saving_bytes": sum(p["saved_bytes"] for p in pages),
              "rejections": ["stale source", "missing archive", "truncated archive"],
              "upload_handler": "actual production function; exact gzip body and Content-Encoding/UTF-8 checked"}
    target = ROOT / "build/github-sync-20261026/transfer-web-assets/report.json"
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
