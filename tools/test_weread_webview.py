#!/usr/bin/env python3
"""中文：本机临时HTTP与真实WKWebView微信读书测试，不访问账户或设备。
English: Run a temporary local HTTP server and real WKWebView WeRead tests, without accounts or devices.
"""
from pathlib import Path
import argparse
from functools import partial
from http.server import ThreadingHTTPServer, SimpleHTTPRequestHandler
import json
import subprocess
import threading
ROOT=Path(__file__).resolve().parents[1]
class Quiet(SimpleHTTPRequestHandler):
    def log_message(self,*args): pass
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output',type=Path,default=ROOT/'build/weread-explore-webview')
args=parser.parse_args();output=args.output.resolve();output.mkdir(parents=True,exist_ok=True)
binary=output/'webview-test'
subprocess.run(['swiftc','-parse-as-library',str(ROOT/'tools/test_weread_webview.swift'),'-o',str(binary)],check=True)
server=ThreadingHTTPServer(('127.0.0.1',0),partial(Quiet,directory=str(ROOT/'components/read_pico_transfer')))
thread=threading.Thread(target=server.serve_forever,daemon=True);thread.start()
try:
    result=subprocess.run([str(binary),str(output),f'http://127.0.0.1:{server.server_address[1]}'],text=True,capture_output=True,timeout=120)
    (output/'stdout.log').write_text(result.stdout+result.stderr)
    print(result.stdout,end='');print(result.stderr,end='')
    result.check_returncode()
    report=json.loads((output/'report.json').read_text());assert report['status']=='PASS'
finally:
    server.shutdown();server.server_close();thread.join()
