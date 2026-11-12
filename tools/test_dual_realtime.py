#!/usr/bin/env python3
"""Run production control scheduling, progress-loss and durable-ACK regression."""
from pathlib import Path
import argparse
import subprocess

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--sanitize', action='store_true')
args = parser.parse_args()
out = root / 'build/dual-realtime-test'
out.mkdir(parents=True, exist_ok=True)
binary = out / ('test-asan' if args.sanitize else 'test')
flags = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer'] if args.sanitize else []
subprocess.run(['cc', '-std=gnu11', '-O1', '-g', '-Wall', '-Wextra',
                '-Wno-deprecated-declarations', *flags,
                str(root / 'tools/test_dual_realtime.c'), '-o', str(binary)], check=True)
subprocess.run([str(binary)], check=True)
