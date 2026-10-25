#!/usr/bin/env python3
"""中文：编译真实模块返回函数，验证应用集来源及保存失败恢复。/ English: Compile the real module-return function for Apps origin and failed-save recovery."""
from pathlib import Path
import ast
import os
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
games = (root / "main/system/pico_games.c").read_text()
header = (root / "main/system/pico_system.h").read_text()
sdk = (root / "components/read_pico_apps/include/pico_app_sdk.h").read_text()
tree = ast.parse((root / "tools/test_pico_system.py").read_text())
extractor = next(item for item in tree.body if isinstance(item, ast.FunctionDef) and item.name == "function")
namespace = {"re": re, "SOURCE": games}
exec(compile(ast.Module(body=[extractor], type_ignores=[]), "source-extractor", "exec"), namespace)
unit = "#include <assert.h>\n#include <stdbool.h>\n#include <stdio.h>\n"
unit += re.search(r"typedef enum\s*\{[^}]*\}\s*ps_route_t;", header, re.S).group()
unit += re.search(r"enum\s*\{\s*PGA_PAGE_LAUNCHER[^}]*\};", sdk).group()
unit += r'''
static bool s_in_call,s_catalog_dirty,can_save;
static int s_nav,s_pending_route;
static ps_route_t s_origin,route;
static unsigned closes,recoveries,scans;
static bool close_active(void){closes++;return can_save;}
static void clear_pending(void){s_pending_route=-1;}
static void recover_active(void){recoveries++;}
static void ps_navigate(ps_route_t next){route=next;}
static ps_route_t ps_route(void){return route;}
static void ps_games_rescan(void){scans++;}
'''
unit += namespace["function"]("finish_call")
unit += r'''
int main(void){
    const ps_route_t origins[]={PS_APPS,PS_GAMES,PS_SHELF};
    for(unsigned i=0;i<3;i++){
        s_origin=origins[i];route=PS_PLAYGAME;can_save=true;s_nav=PGA_PAGE_LAUNCHER;s_in_call=true;s_catalog_dirty=false;
        unsigned before=closes;finish_call();assert(route==origins[i]&&!s_in_call&&s_nav==-1&&closes==before+1);
        route=PS_APPLICATION;can_save=false;s_nav=PGA_PAGE_LAUNCHER;s_pending_route=-1;before=recoveries;
        finish_call();assert(route==PS_APPLICATION&&s_pending_route==(int)origins[i]&&recoveries==before+1);
    }
    s_origin=PS_APPS;s_nav=PGA_PAGE_ARCADE;route=PS_PLAYGAME;s_catalog_dirty=true;finish_call();
    assert(route==PS_ARCADEGAME&&s_origin==PS_APPS&&scans==1);
    puts("PASS: real TF-module return retains Apps/Games/Books origin and failed-save retry destination");
}
'''
with tempfile.TemporaryDirectory(prefix="pico-app-origin-") as temporary:
    path = Path(temporary)
    source, binary = path / "origin.c", path / "origin"
    source.write_text(unit)
    subprocess.run([os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined", str(source), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
