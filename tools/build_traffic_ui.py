#!/usr/bin/env python3
"""网页随TF包分发；固件只保留鉴权引导。/ Ship the phone UI in TF, retaining only an authenticated firmware bootstrap."""
import argparse,json
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def generated():
    text=(ROOT/'apps/assets/website_traffic.html').read_text()
    return '/* 自动生成自网站访问看板手机页。/ Generated from the traffic phone page. */\nstatic const char traffic_ui[] =\n'+''.join(json.dumps(line,ensure_ascii=False).replace('??',r'\?\?')+'\n' for line in text.splitlines(True))+';\n'
def check():
    if (ROOT/'apps/include/traffic_ui.h').read_text()!=generated():raise RuntimeError('Traffic phone UI is stale: run python3 tools/build_traffic_ui.py')
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--check',action='store_true');args=p.parse_args()
    if args.check:check()
    else:(ROOT/'apps/include/traffic_ui.h').write_text(generated())
