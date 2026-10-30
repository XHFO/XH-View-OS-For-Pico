#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""实际设置迁移/字体选择故障回归；NVS和字体IO是主机替身，不接触设备。
Exercise actual preference migration and picker failures with host NVS/font I/O doubles.
"""
from pathlib import Path
import argparse
import os
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def function(source, name):
    match = re.search(r"^static [^\n]+\b" + name + r"\([^\n]*\) \{", source, re.M)
    assert match, name
    start, at, depth, quote = match.start(), match.end(), 1, None
    while depth:
        ch = source[at]
        if quote:
            if ch == "\\":
                at += 2
                continue
            if ch == quote:
                quote = None
        elif source.startswith("//", at):
            at = source.index("\n", at)
            continue
        elif source.startswith("/*", at):
            at = source.index("*/", at) + 2
            continue
        elif ch in "\"'":
            quote = ch
        elif ch == "{":
            depth += 1
        elif ch == "}":
            depth -= 1
        at += 1
    return source[start:at]


PRELUDE = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "test_nvs.h"
#include "ttf_font.h"
static unsigned checks, commits, writes, opens;
#define CHECK(c) do { checks++; if(!(c)){fprintf(stderr,"Preference check failed %s:%d: %s\n",__FILE__,__LINE__,#c);abort();} } while(0)
#if !defined(__APPLE__)
static size_t strlcpy(char *out,const char *in,size_t cap){size_t n=strlen(in);if(cap){size_t copy=n<cap-1?n:cap-1;memcpy(out,in,copy);out[copy]=0;}return n;}
#endif
static bool has_font,has_version,fail_init,fail_open,fail_commit,fail_set;
static char durable_font[256],staged_font[256];
static uint8_t durable_version,staged_version;
static bool staged_has_font,staged_has_version;
static esp_err_t nvs_flash_init(void){return fail_init?ESP_FAIL:ESP_OK;}
static esp_err_t nvs_open(const char *ns,int mode,nvs_handle_t *h){
    CHECK(!strcmp(ns,"read_pico"));opens++;if(fail_open)return ESP_FAIL;
    *h=mode;staged_has_font=has_font;staged_has_version=has_version;
    strcpy(staged_font,durable_font);staged_version=durable_version;return ESP_OK;
}
static void nvs_close(nvs_handle_t h){(void)h;}
static esp_err_t nvs_get_str(nvs_handle_t h,const char *key,char *out,size_t *cap){
    (void)h;CHECK(!strcmp(key,"font"));if(!has_font)return ESP_FAIL;
    size_t n=strlen(durable_font)+1;if(*cap<n){*cap=n;return ESP_FAIL;}
    memcpy(out,durable_font,n);*cap=n;return ESP_OK;
}
static esp_err_t nvs_get_u8(nvs_handle_t h,const char *key,uint8_t *out){
    (void)h;if(strcmp(key,"font_ver")||!has_version)return ESP_FAIL;
    *out=durable_version;return ESP_OK;
}
static esp_err_t nvs_set_str(nvs_handle_t h,const char *key,const char *value){
    CHECK(h==NVS_READWRITE&&!strcmp(key,"font"));writes++;if(fail_set)return ESP_FAIL;
    strlcpy(staged_font,value,sizeof(staged_font));staged_has_font=true;return ESP_OK;
}
static esp_err_t nvs_set_u8(nvs_handle_t h,const char *key,uint8_t value){
    CHECK(h==NVS_READWRITE);writes++;if(fail_set)return ESP_FAIL;
    if(!strcmp(key,"font_ver")){staged_version=value;staged_has_version=true;}return ESP_OK;
}
static esp_err_t nvs_commit(nvs_handle_t h){
    CHECK(h==NVS_READWRITE);commits++;if(fail_commit)return ESP_FAIL;
    strcpy(durable_font,staged_font);durable_version=staged_version;
    has_font=staged_has_font;has_version=staged_has_version;return ESP_OK;
}
#include "settings-under-test.c"

typedef enum {APP_REDRAW_NONE,APP_REDRAW_FULL} app_redraw_t;
typedef struct {char path[160];} ttf_font_item_t;
typedef struct {bool present,mounted,exported;} read_pico_sd_info_t;
static read_pico_sd_info_t media={true,true,false};
static bool suspended,fail_font,wrong_success;
static unsigned font_opens;
static char actual[160]="builtin";
static const ttf_font_item_t catalog[]={ {TTF_FONT_DEFAULT_SD},{"/sdcard/fonts/custom.ttf"} };
static void bench_reset(void){}
static const char *ttf_font_path(void){return actual;}
static bool ttf_font_is_builtin(void){return !strncmp(actual,"builtin",7);}
static bool ttf_font_sd_suspended(void){return suspended;}
static const ttf_font_item_t *ttf_font_item(int i){return i>=0&&i<2?&catalog[i]:NULL;}
static int read_pico_sd_get_info(read_pico_sd_info_t *out){*out=media;return ESP_OK;}
static esp_err_t ttf_font_open(const char *path){
    font_opens++;if(fail_font)return ESP_FAIL;if(wrong_success)return ESP_OK;
    strlcpy(actual,path,sizeof(actual));return ESP_OK;
}
static esp_err_t ttf_font_open_builtin(void){return ttf_font_open("builtin");}
'''

CASES = r'''
static void reboot(void){strlcpy(s_font,TTF_FONT_DEFAULT_SD,sizeof(s_font));s_font_revision=0;app_settings_init();}
static void seed(const char *path,int version){
    fail_init=fail_open=fail_commit=fail_set=false;
    has_font=path!=NULL;strlcpy(durable_font,path?path:"",sizeof(durable_font));
    has_version=version>=0;durable_version=(uint8_t)(version<0?0:version);
}
int main(void){
    seed(NULL,-1);reboot();CHECK(!strcmp(app_settings_font_path(),TTF_FONT_DEFAULT_SD));
    CHECK(has_version&&durable_version==1&&has_font&&!strcmp(durable_font,TTF_FONT_DEFAULT_SD));
    unsigned done=commits;reboot();CHECK(commits==done);
    const char *paths[]={"","builtin","builtin-serif","builtin-kai","/sdcard/fonts/custom.ttf"};
    for(int v=-1;v<=1;v++)for(unsigned i=0;i<sizeof(paths)/sizeof(paths[0]);i++){
        seed(paths[i],v);reboot();const char *expected=v<1&&i<2?TTF_FONT_DEFAULT_SD:paths[i];
        CHECK(!strcmp(app_settings_font_path(),expected));
        CHECK(has_version&&durable_version==1&&!strcmp(durable_font,expected));
        done=commits;reboot();CHECK(!strcmp(app_settings_font_path(),expected)&&commits==done);
    }
    seed("",-1);fail_init=true;done=writes;reboot();CHECK(!strcmp(app_settings_font_path(),TTF_FONT_DEFAULT_SD)&&writes==done);
    seed("",-1);fail_open=true;done=writes;reboot();CHECK(!strcmp(app_settings_font_path(),TTF_FONT_DEFAULT_SD)&&writes==done);
    seed("",-1);fail_commit=true;reboot();CHECK(!has_version&&!strcmp(durable_font,""));
    fail_commit=false;reboot();CHECK(has_version&&!strcmp(durable_font,TTF_FONT_DEFAULT_SD));
    seed("",-1);fail_set=true;reboot();CHECK(!has_version&&!strcmp(durable_font,""));
    fail_set=false;reboot();CHECK(has_version&&!strcmp(durable_font,TTF_FONT_DEFAULT_SD));
    seed(TTF_FONT_DEFAULT_SD,1);reboot();app_settings_set_font_path("");
    CHECK(app_settings_font_revision()==1&&!strcmp(app_settings_font_path(),""));
    reboot();CHECK(!strcmp(app_settings_font_path(),"")&&!app_settings_font_revision());
    app_settings_set_font_path("");app_settings_set_font_path("");CHECK(app_settings_font_revision()==2);

    app_settings_set_font_path(TTF_FONT_DEFAULT_SD);strcpy(actual,"builtin-serif");CHECK(!font_is_current(0));
    CHECK(pick(0)==APP_REDRAW_FULL&&!strcmp(actual,"builtin")&&!strcmp(app_settings_font_path(),""));
    CHECK(font_is_current(0));reboot();CHECK(!strcmp(app_settings_font_path(),""));
    for(int gate=0;gate<4;gate++){
        media=(read_pico_sd_info_t){true,true,false};suspended=false;
        if(gate==0)media.present=false;if(gate==1)media.mounted=false;if(gate==2)media.exported=true;if(gate==3)suspended=true;
        unsigned revision=app_settings_font_revision(),before=font_opens;
        CHECK(pick(1)==APP_REDRAW_FULL&&font_opens==before&&app_settings_font_revision()==revision);
        CHECK(!strcmp(app_settings_font_path(),""));
    }
    media=(read_pico_sd_info_t){true,true,false};suspended=false;
    fail_font=true;unsigned revision=app_settings_font_revision();
    CHECK(pick(1)==APP_REDRAW_FULL&&app_settings_font_revision()==revision&&!strcmp(app_settings_font_path(),""));
    fail_font=false;wrong_success=true;strcpy(actual,"/sdcard/fonts/old.ttf");
    CHECK(pick(1)==APP_REDRAW_FULL&&app_settings_font_revision()==revision&&!strcmp(app_settings_font_path(),""));
    wrong_success=false;CHECK(pick(1)==APP_REDRAW_FULL&&!strcmp(actual,TTF_FONT_DEFAULT_SD)&&!strcmp(app_settings_font_path(),TTF_FONT_DEFAULT_SD));
    CHECK(app_settings_font_revision()==revision+1&&font_is_current(1));
    revision=app_settings_font_revision();CHECK(pick(1)==APP_REDRAW_FULL&&app_settings_font_revision()==revision+1);
    revision=app_settings_font_revision();fail_font=true;CHECK(pick(0)==APP_REDRAW_FULL&&app_settings_font_revision()==revision&&!strcmp(app_settings_font_path(),TTF_FONT_DEFAULT_SD));
    fail_font=false;CHECK(pick(2)==APP_REDRAW_FULL&&!strcmp(app_settings_font_path(),"/sdcard/fonts/custom.ttf"));
    reboot();CHECK(!strcmp(app_settings_font_path(),"/sdcard/fonts/custom.ttf"));
    revision=app_settings_font_revision();CHECK(pick(99)==APP_REDRAW_NONE&&app_settings_font_revision()==revision);
    printf("PASS: font defaults/migration/picker %u checks; NVS failures, explicit builtin, actual-path guard, SD ownership gates; no device access\n",checks);
    return 0;
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sanitize", action="store_true")
    args = parser.parse_args()
    font_header = (ROOT / "main/font/ttf_font.h").read_text()
    default = re.search(r"^#define TTF_FONT_DEFAULT_SD [^\n]+", font_header, re.M).group()
    picker = (ROOT / "main/apps/app_font_pick.c").read_text()
    with tempfile.TemporaryDirectory(prefix="pico-font-preferences-") as temporary:
        build = Path(temporary)
        (build / "settings.h").write_text((ROOT / "main/settings.h").read_text())
        (build / "settings-under-test.c").write_text((ROOT / "main/settings.c").read_text())
        (build / "ttf_font.h").write_text('#pragma once\n' + default + '\n#define TTF_FONT_BUILTIN "builtin"\n')
        (build / "test_nvs.h").write_text(
            '#pragma once\n#include <stdint.h>\ntypedef int esp_err_t;\ntypedef int nvs_handle_t;\n'
            '#define ESP_OK 0\n#define ESP_FAIL -1\n#define NVS_READONLY 1\n#define NVS_READWRITE 2\n'
        )
        for name in ("nvs.h", "nvs_flash.h"):
            (build / name).write_text('#pragma once\n#include "test_nvs.h"\n')
        (build / "esp_log.h").write_text('#pragma once\n#define ESP_LOGI(...) ((void)0)\n#define ESP_LOGW(...) ((void)0)\n')
        unit = PRELUDE + function(picker, "font_is_current") + '\n' + function(picker, "pick") + CASES
        (build / "test.c").write_text(unit)
        cmd = [os.environ.get("CC", "cc"), "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-g"]
        if args.sanitize:
            cmd += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
        executable = build / "font-preferences"
        subprocess.run(cmd + ["-I" + str(build), str(build / "test.c"), "-o", str(executable)], check=True)
        subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    main()
