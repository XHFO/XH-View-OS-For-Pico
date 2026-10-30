#!/usr/bin/env python3
"""Validate and copy the lossless precompressed page; builds use only the Python standard library.

Regeneration is explicit and requires zopfli 0.4.0 with the parameters pinned below.
"""
import argparse
import gzip
import importlib.metadata
from pathlib import Path
import zlib


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--archive", type=Path, help="override the checked-in archive location")
    parser.add_argument("--regenerate", action="store_true", help="regenerate with pinned zopfli 0.4.0")
    args = parser.parse_args()
    archive = args.archive or args.source.parent / "assets" / (args.source.name + ".gz")
    raw = args.source.read_bytes()
    if args.regenerate:
        if importlib.metadata.version("zopfli") != "0.4.0":
            raise SystemExit("Regeneration requires zopfli==0.4.0")
        import zopfli.gzip
        encoded = zopfli.gzip.compress(raw, numiterations=30)
        archive.parent.mkdir(parents=True, exist_ok=True)
        archive.write_bytes(encoded)
    try:
        encoded = archive.read_bytes()
        decoded = gzip.decompress(encoded)
    except (OSError, EOFError, zlib.error) as error:
        raise SystemExit(f"Missing or corrupt precompressed page {archive}: {error}") from error
    if decoded != raw:
        raise SystemExit(f"Stale precompressed page {archive}: regenerate from {args.source}")
    if encoded[:8] != bytes.fromhex("1f8b080000000000"):
        raise SystemExit(f"Noncanonical gzip header in {archive}: expected mtime=0 without filename")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(encoded)


if __name__ == "__main__":
    main()
