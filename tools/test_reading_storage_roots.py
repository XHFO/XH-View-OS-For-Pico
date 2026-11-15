#!/usr/bin/env python3
"""Exercise production reading-root selection with both real-device storage roots."""
from pathlib import Path
import subprocess, tempfile
root=Path(__file__).resolve().parents[1]
source=(root/'main/system/pico_system.c').read_text()
a=source.index('PICO_STORE_COLD bool ps_storage_record_roots(')
b=source.index('PICO_STORE_COLD bool ps_load_at(',a)
code=r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#define PICO_STORE_COLD
#define ESP_OK 0
typedef struct {char path[80];bool is_flash;} book_store_root_t;
static bool s_store_lost,blocked,sd_available=true,flash_available=true,reversed;
static const char *s_store_root="/flash/books/.pico-system";
static bool record_root_allowed(const char *r){return !blocked&&r&&*r;}
static int book_store_roots(book_store_root_t *out,int *count){
 *count=0;
 if(reversed&&flash_available)out[(*count)++]=(book_store_root_t){"/flash/books",true};
 if(sd_available)out[(*count)++]=(book_store_root_t){"/sdcard/books",false};
 if(!reversed&&flash_available)out[(*count)++]=(book_store_root_t){"/flash/books",true};
 return 0;
}
'''+source[a:b]+r'''
int main(void){
 char current[160],other[160];bool sd=false;
 for(int reverse=0;reverse<2;reverse++){
  reversed=reverse;assert(ps_storage_record_roots(current,other,sizeof(current),&sd)&&sd);
  assert(!strcmp(current,"/sdcard/books/.pico-system")&&!strcmp(other,s_store_root));
 }
 assert(!strcmp(s_store_root,"/flash/books/.pico-system"));
 flash_available=false;assert(ps_storage_record_roots(current,other,sizeof(current),&sd)&&sd&&!*other);
 flash_available=true;sd_available=false;
 assert(ps_storage_record_roots(current,other,sizeof(current),&sd)&&!sd&&!*other);
 assert(!strcmp(current,s_store_root));
 flash_available=false;assert(!ps_storage_record_roots(current,other,sizeof(current),&sd));
 sd_available=flash_available=true;
 blocked=true;assert(!ps_storage_record_roots(current,other,sizeof(current),&sd));blocked=false;
 s_store_lost=true;assert(!ps_storage_record_roots(current,other,sizeof(current),&sd));s_store_lost=false;
 assert(!ps_storage_record_roots(current,other,4,&sd));
 assert(!ps_storage_record_roots(NULL,other,sizeof(current),&sd));
 return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='reading-roots-') as d:
 p=Path(d)/'test.c';p.write_text(code);exe=p.with_suffix('')
 subprocess.run(['cc','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
print('PASS: production root selection prefers TF with flash-bound settings, either enumeration order, one/no media, blocked/lost and bounded paths')
