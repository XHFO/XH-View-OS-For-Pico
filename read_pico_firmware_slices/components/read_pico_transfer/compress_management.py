#!/usr/bin/env python3
"""确定性压缩嵌入网页。 / Deterministically compress embedded web pages."""
import gzip
from pathlib import Path
import sys
Path(sys.argv[2]).write_bytes(gzip.compress(Path(sys.argv[1]).read_bytes(), compresslevel=9, mtime=0))
