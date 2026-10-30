#!/usr/bin/env python3
"""真实离页函数和微信读书工作器的准入/联网生命周期回归；独立TF、NVS与HTTP夹具。
Exercise production page-exit/admission and WeRead worker lifecycles with private TF/NVS/HTTP fixtures.
"""
from pathlib import Path
import os
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def function(source, name):
    match = re.search(r'^(?:static\s+)?(?:PICO_STORE_COLD\s+)?void ' + re.escape(name) + r'\([^;]*?\)\s*\{', source, re.M)
    if not match:
        raise RuntimeError('Missing production function: ' + name)
    begin = source.index('{', match.start())
    depth, at = 1, begin + 1
    while depth:
        depth += (source[at] == '{') - (source[at] == '}')
        at += 1
    return source[match.start():at]


PRELUDE = r'''
#define PICO_WEREAD_HOST_TEST 1
#include "@ROOT@/main/system/pico_weread.c"
#include <assert.h>
void wr_test_cover_clear(void){}
void wr_test_cover_prepare(uint32_t id,const char *url,const atomic_bool *cancelled){(void)id;(void)url;(void)cancelled;}
static unsigned checks,shutdown_calls,shutdown_retries,input_cancels,reader_exits,home_exits,games_exits,sensor_sleeps,rotation;
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);abort();}}while(0)
static char fixture_root[256],fixture_key[PS_WEREAD_KEY_MAX+1];
static uint8_t fixture_selection[WR_SELECTION_BYTES];static size_t fixture_selection_bytes;
static atomic_bool fixture_hold,fixture_entered,fixture_release;
static atomic_uint fixture_http_calls;
static bool s_network_resume_pending,s_tracking,s_fan,s_fan_candidate;
static bool cleanup_paused,fixture_release_transfer,s_transfer_prepared,s_store_lost;
static struct {char path[160];} s_transfer_root;
static uint64_t s_transfer_free;static size_t s_transfer_limit;
static unsigned fixture_transfer_releases;
void ps_transfer_finished(unsigned changed_count);
static void checkpoint_invalidate(void){}
static void display_set_bulk_io(bool on){CHECK(!on);}
static void ttf_font_suspend_sd(bool on){CHECK(!on);}
static void book_store_notify_changed(void){CHECK(s_pause_leases>0);}
static void ps_games_rescan(void){CHECK(s_pause_leases>0);CHECK(!admit(false,NULL,0,0));}
static void save_config(void){CHECK(s_pause_leases>0);CHECK(!admit(false,NULL,0,0));}
static const int app_book=1;
#define PS_ROUTE_COUNT 100
#define EPD_ROT_INVERTED_PORTRAIT 3
#define ESP_LOGW(...) ((void)0)
typedef struct {bool sensor_ready;void *acc;const void *request_app;} app_ctx_t;
bool wr_test_wifi(void){return true;}
bool wr_test_media(uint32_t *generation){if(generation)*generation=7;return true;}
const char *wr_test_directory(void){return fixture_root;}
uint32_t wr_test_random(void){return 123;}
bool wr_test_nv_get(const char *name,void *out,size_t *size){
 const void *data=NULL;size_t n=0;
 if(!strcmp(name,"api_key")&&fixture_key[0]){data=fixture_key;n=strlen(fixture_key)+1;}
 else if(!strcmp(name,"selected")&&fixture_selection_bytes){data=fixture_selection;n=fixture_selection_bytes;}else return false;
 if(out&&*size<n)return false;if(out)memcpy(out,data,n);*size=n;return true;
}
bool wr_test_nv_set(const char *name,const void *data,size_t size){
 if(strcmp(name,"selected")||size>sizeof(fixture_selection))return false;
 memcpy(fixture_selection,data,size);fixture_selection_bytes=size;return true;
}
bool wr_test_nv_authorize(const char *key){snprintf(fixture_key,sizeof(fixture_key),"%s",key);fixture_selection_bytes=0;return true;}
bool wr_test_nv_clear(void){wipe(fixture_key,sizeof(fixture_key));fixture_selection_bytes=0;return true;}
const char *wr_test_http(const char *key,const char *request,char *response,size_t capacity,size_t *used,int *status){
 CHECK(!strcmp(key,"wrk-lifecycle-fixture"));atomic_fetch_add(&fixture_http_calls,1);
 if(atomic_load(&fixture_hold)){atomic_store(&fixture_entered,true);while(!atomic_load(&fixture_release)){struct timespec t={0,1000000};nanosleep(&t,NULL);}}
 const char *answer=strstr(request,"/shelf/sync")?"{\"books\":[{\"bookId\":\"book1\",\"title\":\"Fixture book\",\"author\":\"Fixture author\"}]}":
  strstr(request,"/book/bookmarklist")?"{\"updated\":[{\"type\":1,\"markText\":\"Fixture highlight\",\"chapterUid\":1}],\"chapters\":[]}":
  strstr(request,"/review/list/mine")?"{\"reviews\":[],\"hasMore\":0,\"synckey\":0}":NULL;
 CHECK(answer);*used=strlen(answer);CHECK(*used<capacity);memcpy(response,answer,*used+1);*status=200;return NULL;
}
static void ps_global_cancel_input(void){input_cancels++;s_fan_candidate=false;}
static bool ps_connectivity_shutdown(void){
 shutdown_calls++;
 if(fixture_release_transfer){fixture_release_transfer=false;CHECK(s_pause_leases==2);ps_transfer_finished(1);fixture_transfer_releases++;CHECK(s_pause_leases==1);}
 // 真实准入不能在停网窗口启动任务；不仅检查一个mock busy布尔值。/ Actual admission must reject work throughout shutdown, beyond a mocked busy boolean.
 CHECK(s_pause_leases>0);CHECK(!ps_weread_busy());CHECK(!admit(false,NULL,0,0));
 return shutdown_calls>shutdown_retries;
}
static void ps_reader_enter(int route){CHECK(route==PS_ROUTE_COUNT);reader_exits++;CHECK((s_pause_leases>0)==cleanup_paused);}
static void ps_home_enter(int route){CHECK(route==PS_ROUTE_COUNT);home_exits++;}
static void ps_games_enter(int route){CHECK(route==PS_ROUTE_COUNT);games_exits++;}
static void read_pico_sensor_sleep(void *sensor){CHECK(sensor==(void*)1);sensor_sleeps++;}
static void epd_set_rotation(int value){rotation=(unsigned)value;}
'''

TESTS = r'''
static void wait_done(void){
 for(unsigned i=0;i<10000&&ps_weread_busy();i++){struct timespec t={0,1000000};nanosleep(&t,NULL);}
 CHECK(!ps_weread_busy());ps_weread_status_t *status=malloc(sizeof(*status));CHECK(status);ps_weread_get_status(status);
 CHECK(!strcmp(status->state,"done"));CHECK(!*status->error);free(status);
}
int main(int argc,char **argv){
 CHECK(argc==2);snprintf(fixture_root,sizeof(fixture_root),"%s/weread",argv[1]);
 ps_weread_init();char output[32768];const char *body="{\"api_key\":\"wrk-lifecycle-fixture\"}";
 CHECK(ps_weread_handle("POST","/api/manage/pico/weread/authorize","application/json",body,strlen(body),output,sizeof(output))==200);
 app_ctx_t ctx={.sensor_ready=true,.acc=(void*)1,.request_app=&app_book};
 // 没有活动任务仍走原先停网，并且所有收尾完成后才解除准入屏障。/ Idle navigation retains ordinary shutdown, releasing admission only after cleanup.
 cleanup_paused=true;s_network_resume_pending=s_tracking=s_fan=s_fan_candidate=true;shutdown_retries=1;system_exit(&ctx);
 CHECK(shutdown_calls==2);CHECK(!s_pause_leases);CHECK(!s_network_resume_pending);CHECK(!s_tracking&&!s_fan&&!s_fan_candidate);
 CHECK(input_cancels==1&&reader_exits==1&&home_exits==1&&games_exits==1&&sensor_sleeps==1&&rotation==EPD_ROT_INVERTED_PORTRAIT);
 CHECK(admit(false,NULL,0,0));wait_done();
 // 真后台同步停在虚拟官方HTTP；切到真实读书页立即完成，不能停STA或等待worker。/ Hold a real sync in fake official HTTP; navigation to the reading app must preserve STA without joining the worker.
 atomic_store(&fixture_hold,true);atomic_store(&fixture_release,false);atomic_store(&fixture_entered,false);
 const char ids[WRS_BOOK_MAX][64]={{"book1"}};CHECK(admit(true,ids,1,0));
 for(unsigned i=0;i<10000&&!atomic_load(&fixture_entered);i++){struct timespec t={0,1000000};nanosleep(&t,NULL);}
 CHECK(atomic_load(&fixture_entered));CHECK(ps_weread_busy());cleanup_paused=false;unsigned before=shutdown_calls;
 s_network_resume_pending=s_tracking=s_fan=s_fan_candidate=true;system_exit(&ctx);
 CHECK(shutdown_calls==before);CHECK(ps_weread_busy());CHECK(!s_pause_leases);CHECK(!atomic_load(&fixture_release));
 CHECK(!s_network_resume_pending);CHECK(!s_tracking&&!s_fan&&!s_fan_candidate);CHECK(reader_exits==2&&sensor_sleeps==2);
 atomic_store(&fixture_hold,false);atomic_store(&fixture_release,true);wait_done();
 ps_weread_status_t *status=malloc(sizeof(*status));CHECK(status);ps_weread_get_status(status);CHECK(status->synced_books==1&&status->synced_notes==1);free(status);
 char path[300];snprintf(path,sizeof(path),"%s/index.dat",fixture_root);CHECK(valid_index(path));
 // 同步结束后下一次普通离页重新获得停网屏障，不永久泄漏日常连接。/ A later idle departure regains the barrier; preserving active work does not permanently skip shutdown.
 cleanup_paused=true;before=shutdown_calls;shutdown_retries=0;ctx.sensor_ready=false;system_exit(&ctx);
 CHECK(shutdown_calls==before+1);CHECK(!s_pause_leases);CHECK(sensor_sleeps==2);CHECK(admit(false,NULL,0,0));wait_done();
 // 真实传书收尾释放自己的内层租约，不能把system_exit的外层屏障一并解除。/ Production transfer completion releases only its lease; page-exit admission remains blocked.
 before=shutdown_calls;CHECK(ps_weread_pause());CHECK(s_pause_leases==1);s_transfer_prepared=true;fixture_release_transfer=true;
 system_exit(&ctx);CHECK(shutdown_calls==before+1);CHECK(fixture_transfer_releases==1&&!s_transfer_prepared&&!s_pause_leases);
 CHECK(admit(false,NULL,0,0));wait_done();
 printf("PASS production system_exit + transfer-finish + WeRead admission lifecycle: %u checks\n",checks);return 0;
}
'''

with tempfile.TemporaryDirectory(prefix='pico-weread-lifecycle-') as temporary:
    private = Path(temporary)
    private.chmod(0o700)
    source, binary = private / 'lifecycle.c', private / 'lifecycle'
    system = (ROOT / 'main/system/pico_system.c').read_text()
    cold_macro = re.search(r'^#if [^\n]+\n#define PICO_STORE_COLD[^\n]*\n#else\n#define PICO_STORE_COLD[^\n]*\n#endif', system, re.M)
    if not cold_macro:
        raise RuntimeError('Missing production PICO_STORE_COLD definition')
    source.write_text(cold_macro.group() + '\n' + PRELUDE.replace('@ROOT@', str(ROOT)) + '\n' + '\n'.join(function(system, name) for name in ('ps_transfer_finished','system_exit')) + '\n' + TESTS)
    subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-Wall', '-Wextra', '-Werror', '-O1', '-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-I', str(ROOT / 'managed_components/espressif__cjson/cJSON'), str(source), str(ROOT / 'components/read_pico_home_link/home_link_json.c'), '-pthread', '-lm', '-o', str(binary)], check=True)
    subprocess.run([str(binary), str(private)], check=True, timeout=30)
