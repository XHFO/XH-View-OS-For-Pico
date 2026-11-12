/*
 * 书架、家庭、小游戏的统一原生系统壳；所有页面共享返回和两角快捷操作。
 * Native shell for Books, Home and Games, with shared Back and corner shortcuts.
 * 冻结：用户授权原生化可自定义启动器；设置独立返回来源，中键单击返回/双击切页/长按书架，两角透明且无中央返回。
 * Frozen: User-authorized customizable native launcher; independent settings origin, center Back/switcher/shelf and transparent corners.
 * 用户修订：设置采用六个用途分组，子页返回先回原分组再回设置根；关于明确标注 XH View OS For Pico 为先行预览测试版，协议版本号保持独立。
 * User revision: Six purpose-based settings groups retain their parent on Back; About identifies XH View OS For Pico as a preview test build, keeping protocol versions separate.
 * 用户修订：TF 应用返回已有设置来源时保留原分组，修复家庭/打印机模块退回根页；显式设置入口仍打开根页。
 * User revision: TF apps returning to settings history retain the source group, fixing Home/printer returns; explicit Settings still opens the root.
 * 用户修订：所有已绘制的页头返回箭头共用实体返回流程，先关闭最内层状态并保留草稿保存与存储交接检查；根导航与正文无隐形返回热区。
 * User revision: Every visible heading Back uses physical Back semantics, dismissing the innermost state with draft/ownership guards; roots and text have no invisible Back target.
 * 用户修订：双机自动方向跟随主机，两角色各有180度偏移，独立于系统方向与锁定；退出恢复系统偏好，横屏姿态不重排。
 * User revision: Paired auto orientation follows the primary with separate 180-degree role offsets; system direction/lock are preserved on exit, and landscape poses do not reflow.
 * 用户修订：真实图书等产品子页空闲时也处理可信USB引导，不为配网离开当前页；触摸、浮层与未完成笔迹期间延后。
 * User revision: Idle product child pages also service trusted USB setup without leaving the page; defer during touch, overlays and unfinished ink.
 * 冻结：用户确认两角三层3/5/6共14个直达功能位置；全部可替换，左右镜像共用一套位置，无分类与子菜单。
 * Frozen: The approved 3/5/6 rings hold 14 direct, replaceable actions; mirrored corners share one flat arrangement.
 * 用户修订：滑动复用16灰阶静态基底，仅以GL16更新光圈和固定提示区；同选项不推屏，视觉大光圈不参与命中。
 * User revision: Reuse a 16-gray base and GL16 cursor/prompt damage; unchanged choices do not present and visual halos never affect hits.
 * 冻结：用户要求书架翻页局部刷新；稳定书架仅推实际变化区域，保留灰阶与区域周期清理，转向/强刷/错误仍走整屏恢复。
 * Frozen: Shelf paging presents only the changed region, retaining grayscale and periodic regional cleanup; rotation, forced refresh and errors retain full recovery.
 * 用户修订：所有稳定界面保留原生16灰阶；输入与笔迹仅临时二值化损伤区，停止操作后统一GL16定稿，禁止整页阈值化。
 * 冻结：键盘与TF画板使用官方连续DU，一次只扫一相；切页/旋转/浮层中断余相时先强刷重建基准。笔迹逐坐标派发，普通控件保留节流。
 * Frozen: Keyboard and TF drawing use official one-phase continuous DU; interrupted drive requires a full baseline on navigation, rotation or overlays. Dispatch each new ink coordinate; retain normal control throttling.
 * User revision: All stable UI retains native 16 grays; input/ink threshold only transient damage and settle with GL16 after inactivity, never thresholding whole pages.
 * 用户修订：显式存档只跳过与有效主档逐字节相同的内容；电源检查点可复用同一存储会话的成功提交，移交/换卡/维护后失效。
 * User revision: Explicit saves skip only byte-identical valid primaries; power checkpoints may reuse successful commits only until transfer, media or maintenance invalidation.
 * 用户测试：保持唤醒开启时抑制空闲及横屏充电自动休眠，手动休眠仍有效。
 * User testing: keep-awake suppresses idle and dock standby while preserving manual sleep.
 * 冻结：原位编排支持长按拖动交换、空位、撤销；完成才写quick_v5，取消不落盘，旧启用动作保留为未放置。
 * Frozen: In-place editing supports hold-drag swaps, empty slots and undo; Done saves v5, Cancel writes nothing, legacy extras remain unplaced.
 * 冻结：家庭与打印状态只取真实服务快照；未配置为空，旧示例仅保留内部测试。WiFi与传书使用单worker真实连接，日常WiFi不依赖传书页。
 * 传书离页先汇合HTTP，再恢复文件读取；UI格式化两次确认，可信USB维护须明确授权。
 * Frozen: Home/printer pages use real service snapshots, with unconfigured empty states and test-only fixtures; one worker owns real WiFi/transfer, independent of the transfer page.
 * Join transfers before restoring files; UI erasure requires two confirmations, and trusted USB maintenance requires explicit authorization.
 * 冻结：有效已同步PMU RTC同时驱动界面与ESP系统钟；桥接在硬件轮询中执行，预览不写电脑钟。
 * Frozen: Valid synchronized PMU RTC feeds both UI and ESP system time through hardware polling; preview never sets host time.
 * 用户修订：后台USB仅暂停TF消费者，设置/蓝牙/全局操作保持可用；普通导航不弹出存储确认。
 * User revision: Background USB pauses TF consumers while settings, Bluetooth and global controls remain usable; ordinary navigation never opens eject confirmation.
 * 冻结：WiFi内容请求由UI线程保存并暂停TF消费者后授予租约，HTTP仅申请/归还；停服务先取消待授予请求，换卡与归还完成前禁本机文件访问。
 * Frozen: UI grants WiFi content leases after saving/pausing TF consumers; HTTP only acquires/releases. Cancel pending grants before server join and retain local file gates until media-safe reclaim.
 * 用户修订：挂载恢复重建字体、书架与应用目录；仅同一媒体代际解除原存档门禁，换卡不写回旧会话。
 * User revision: Restore fonts, shelf and app discovery after remount; only the bound media generation may resume writes from this session.
 */
#include "pico_system.h"
#include "pico_dual.h"
#include "pico_updates.h"
#include "pico_reader.h"
#include "pico_theme.h"
#include "pico_bluetooth.h"
#include "pico_datetime.h"
#include "pico_power.h"
#include "pico_sound.h"
#include "pico_navigation.h"
#include "pico_launcher.h"
#include "pico_ink.h"
#include "pico_desktop.h"
#include "pico_weread.h"
#include "../apps/app_book.h"
#include "pico_connectivity.h"
#include "read_pico_connectivity.h"
#include "read_pico_content.h"
#include "read_pico_sd.h"
#include "book_progress.h"
#include <errno.h>
#include <ctype.h>
#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <sys/stat.h>
#include <unistd.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "book_store.h"
#include "display.h"
#include "e0470_epaper_waveform.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#ifdef ESP_PLATFORM
#include "esp_rom_crc.h"
#endif
#include "nvs.h"
#include "read_pico_init.h"
#include "read_pico_pmu.h"
#ifdef ESP_PLATFORM
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_mac.h"
#include "esp_ota_ops.h"
#endif
#include "ttf_font.h"
#include "ui_menu.h"
#include "ui_kit.h"
#include "settings.h"

#define PS_VERSION PICO_FIRMWARE_VERSION
#include "pico_version.h"
#define PS_OS_NAME "XH View OS For Pico"
#define PS_RELEASE_LABEL "双机阅读实验版"
const char *ps_version(void){return PS_VERSION;}
// 仅压缩文件I/O与设置迁移/保存/引导等低频路径；通用绘制、触摸、扇形菜单和ROM校验保留既有速度。/ Size-optimize file I/O and infrequent settings migration/save/setup; retain general rendering, touch, fan-menu and ROM CRC speed.
#if defined(__GNUC__) && !defined(__clang__)
#define PICO_STORE_COLD __attribute__((optimize("Os")))
#else
#define PICO_STORE_COLD
#endif
#define QUICK_COUNT 41
#define QUICK_MAX 41
#define FAN_GROUPS 5
#define FAN_SLOTS 14
#define QUICK_EMPTY 255
#define ARRAY_COUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))
static app_ctx_t* s_ctx;
static ps_route_t s_route = PS_SHELF, s_history[10], s_workspace[2] = { PS_SHELF, PS_HA };
static int s_depth, s_section, s_panel, s_group, s_focus, s_quick_scroll, s_device_page, s_quick_mode;
static bool s_status_percent;
static uint8_t s_wifi_level;
static int64_t s_wifi_sample_ms;
static uint8_t s_status_style;
static bool s_status_style_picker;
static uint8_t s_shelf_card_style;
static bool s_shelf_card_picker;
static bool s_force_full, s_center_down, s_center_hold, s_center_pending, s_center_second, s_overlay_restore;
static int64_t s_center_since, s_center_due;
static const app_desc_t *s_center_app;
static ps_route_t s_center_route;
static struct {
    uint32_t version;
    uint8_t count,ids[QUICK_MAX],group_mask[QUICK_COUNT],order[FAN_GROUPS][QUICK_COUNT];
} s_shortcuts;
typedef struct {
    uint32_t version;
    uint8_t slots[FAN_SLOTS],count,ids[QUICK_MAX];
} quick_saved_t;
typedef struct {
    uint32_t version;
    uint8_t slots[FAN_SLOTS],count,ids[40];
} quick_saved_v5_40_t;
static uint8_t s_quick_slots[FAN_SLOTS];
static const uint8_t s_quick_defaults[FAN_SLOTS]={0,4,11,15,31,3,17,30,9,7,8,33,22,14};
static struct {
    uint8_t slots[FAN_SLOTS],undo[FAN_SLOTS];
    int selected,source,target,side;
    bool active,dragging,suppress_tap,undo_valid,save_failed;
} s_quick_edit;
static struct {
    uint8_t *base;
    const void *owner;
    uint64_t key;
    int rotation,selected;
    bool valid,allocation_attempted;
} s_fan_paint;
static bool s_ready, s_dirty, s_fast, s_tracking, s_long, s_fan, s_fan_candidate;
static bool s_ink_gray_settle;
static int64_t s_ink_gray_due;
static int s_x0, s_y0, s_lastx, s_lasty, s_fan_side, s_fan_selected = -1;
static int64_t s_down_ms, s_move_ms, s_last_input, s_pmu_ms, s_acc_ms, s_toast_until;
static char s_toast[128], s_store_root[160];
static int64_t s_notice_until;
static char s_notice[96];
static bool s_store_is_sd, s_store_lost, s_transfer_prepared, s_network_resume_pending;
static bool s_store_media_known;
static uint32_t s_store_media_generation;
static bool s_external_active,s_external_was_lost;
static uint32_t s_external_generation;
enum { CONTENT_IDLE,CONTENT_REQUEST,CONTENT_GRANTED,CONTENT_RELEASED,CONTENT_CHANGED };
static atomic_uint s_content_state;
static bool s_content_owned,s_content_refresh;
static const app_desc_t* s_legacy;
static const app_desc_t* s_global_app;
static bool s_global_enabled, s_global_claimed, s_book_return, s_real_book_preferred;
// 风险确认持久到明确取消或提交，不依赖计时或长按。/ Explicit confirmation, never timeout/double-tap/hold.
static uint8_t s_update_confirm;
static char s_update_confirm_version[33];
static int s_book_return_depth;
static app_redraw_t s_global_redraw;
static int s_battery = -1;
static bool s_charging, s_clock_valid;
static time_t s_epoch;
static int64_t s_epoch_ms;
static ps_route_t s_sleep_origin = PS_SHELF;
static int s_practice;
static sc7a20h_sample_t s_last_sample;
static bool s_sample_valid;
static int s_orientation_candidate = -1;
static int s_display_rotation = -1;
static uint8_t s_touch_guard = 1;
static uint8_t s_orientation_matches;
static int64_t s_motion_ms, s_sleep_ms;
static int64_t s_editor_render_due;
#ifdef ESP_PLATFORM
static int64_t s_editor_logic_max_us,s_editor_capture_max_ms;
static unsigned s_editor_input_count;
#endif
typedef struct {
    char ssid[48];
    char password[64];
    uint8_t auto_join;
} wifi_t;
static struct {
    uint32_t version;
    uint16_t quick;
    uint8_t locked, landscape, onboard_done, onboard_step;
    uint8_t demo_wifi, demo_ha, wifi_count, wifi_current;
    uint8_t sleep_style;
    uint16_t interval;
    wifi_t wifi[8];
} s_config;
static const struct {
    const char *label;
    ps_icon_t icon;
    int group;
} s_quick[] = {
    {"我的书库",PI_BOOK,0},
    {"继续阅读",PI_BOOK,0},
    {"阅读记录",PI_MARK,2},
    {"阅读排版",PI_SLIDERS,0},
    {"家庭面板",PI_HOME,1},
    {"家庭房间",PI_GRID,1},
    {"实体筛选",PI_SLIDERS,1},
    {"一键休眠",PI_LOCK,4},
    {"Wi-Fi",PI_WIFI,3},
    {"方向锁定",PI_ROTATE,3},
    {"快捷设置",PI_GRID,4},
    {"小游戏",PI_GAME,3},
    {"打印监看",PI_PRINTER,1},
    {"阅读工具",PI_SLIDERS,0},
    {"系统设置",PI_SETTINGS,4},
    {"图书目录",PI_LIST,0},
    {"添加书签",PI_MARK,0},
    {"划线列表",PI_HIGHLIGHT,2},
    {"笔记列表",PI_NOTE,2},
    {"选区划线",PI_HIGHLIGHT,2},
    {"选区批注",PI_NOTE,2},
    {"收藏图书",PI_HEART,0},
    {"导入图书",PI_IMPORT,3},
    {"休眠样式",PI_CLOCK,4},
    {"家庭设置",PI_HOME,1},
    {"2048",PI_GAME,3},
    {"贪吃蛇",PI_GAME,3},
    {"暂停继续",PI_PAUSE,3},
    {"重新开始",PI_REFRESH,3},
    {"游戏规则",PI_BOOK,3},
    {"笔记输入",PI_NOTE,2},
    {"阅读进度",PI_LAYOUT,0},
    {"本书书签",PI_MARK,0},
    {"TF 卡存储",PI_SD,3},
    {"USB 传输",PI_USB,3},
    {"屏幕刷新",PI_REFRESH,4},
    {"电量信息",PI_BATTERY,4},
    {"连接状态",PI_LINK,1},
    {"本书划线",PI_HIGHLIGHT,0},
    {"本书批注",PI_NOTE,0},
    {"贴合防误触",PI_LINK,4}
};

static const char *s_quick_short[QUICK_COUNT]={
    "书架","续读","记录","排版","家庭","房间","筛选","休眠","网络","方向",
    "编排","游戏","打印","工具","设置","目录","加签","划线","笔记","选划",
    "批注","收藏","传书","样式","家设","合成","贪蛇","暂停","重开","规则",
    "输入","进度","书签","存储","U盘","强刷","电量","连接","划线","批注","防触"
};

static void fan_present_reset(void) {
    free(s_fan_paint.base);
    memset(&s_fan_paint,0,sizeof(s_fan_paint));
}

// ROM与主机实现使用同一标准CRC32，保留现有文件头及分块校验语义。/ ROM and host share standard CRC32, preserving headers and incremental validation.
static uint32_t crc32_update(uint32_t previous,const void *data,size_t length) {
#ifdef ESP_PLATFORM
    return esp_rom_crc32_le(previous,data,(uint32_t)length);
#else
    const uint8_t *p=data;uint32_t c=~previous;
    for(size_t i=0;i<length;i++){
        c^=p[i];for(int j=0;j<8;j++)c=(c>>1)^(0xedb88320u&-(c&1u));
    }
    return ~c;
#endif
}
static uint32_t crc32(const void *data,size_t length) { return crc32_update(0,data,length); }
static bool good_key(const char* s) {
    if (!s || !*s || strlen(s) > 40) return false;
    for (; *s; s++)
        if (!((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') || (*s >= '0' && *s <= '9') ||
              *s == '_' || *s == '-'))
            return false;
    return true;
}
static PICO_STORE_COLD bool valid_file(FILE* f, size_t n) {
    uint32_t h[3], c = 0;
    uint8_t bytes[256];
    size_t remaining = n;
    if (fread(h, 1, sizeof(h), f) != sizeof(h) || h[0] != 0x31535058 || (n && h[1] != n) || h[1] > 65536) return false;
    if (!n) remaining = h[1];
    while (remaining) {
        size_t block = remaining < sizeof(bytes) ? remaining : sizeof(bytes);
        if (fread(bytes, 1, block, f) != block) return false;
        c=crc32_update(c,bytes,block);
        remaining -= block;
    }
    return fgetc(f) == EOF && h[2] == c;
}
static PICO_STORE_COLD bool load_file(const char* path, void* out, size_t n) {
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    bool ok = valid_file(f, n);
    if (ok && out) {
        ok = fseek(f, 12, SEEK_SET) == 0 && fread(out, 1, n, f) == n;
    }
    fclose(f);
    return ok;
}
PICO_STORE_COLD bool ps_load(const char* key, void* out, size_t n) {
    if (!good_key(key) || !out || !n || n > 65536 || s_store_lost || ps_storage_busy() || s_transfer_prepared || s_external_active) return false;
    if (s_store_root[0]) {
        char p[256];
        snprintf(p, sizeof(p), "%s/%s.dat", s_store_root, key);
        if (load_file(p, out, n)) return true;
        snprintf(p, sizeof(p), "%s/%s.bak", s_store_root, key);
        if (load_file(p, out, n)) return true;
        return false;
    }
    if (n > 1024 || strlen(key) > 15) return false;
    nvs_handle_t h;
    if (nvs_open("pico_system", NVS_READONLY, &h) != ESP_OK) return false;
    size_t bytes = 0;
    esp_err_t e = nvs_get_blob(h, key, NULL, &bytes);
    if (e == ESP_OK && bytes == n) {
        e = nvs_get_blob(h, key, out, &bytes);
    } else
        e = ESP_ERR_INVALID_SIZE;
    nvs_close(h);
    return e == ESP_OK && bytes == n;
}
static PICO_STORE_COLD bool stored_file_matches(const char *path,const void *data,size_t n,const uint32_t expected[3]) {
    FILE *f=fopen(path,"rb");
    if(!f)return false;
    uint32_t header[3];uint8_t bytes[256];const uint8_t *source=data;
    bool same=fread(header,1,sizeof(header),f)==sizeof(header)&&!memcmp(header,expected,sizeof(header));
    while(same&&n){size_t count=n<sizeof(bytes)?n:sizeof(bytes);
        same=fread(bytes,1,count,f)==count&&!memcmp(bytes,source,count);source+=count;n-=count;}
    if(same)same=fgetc(f)==EOF&&!ferror(f);
    if(fclose(f))same=false;
    return same;
}
static PICO_STORE_COLD bool store_file(const char *root,const char *key,const void *data,size_t n) {
        char p[256];uint32_t h[3]={0x31535058,(uint32_t)n,crc32(data,n)};
        snprintf(p,sizeof(p),"%s/%s.dat",root,key);
        // 不信缓存或备份：当前主档完整匹配才免写，外部替换、坏档和新内容仍提交。/ Skip only an exact current primary, never cached metadata or a matching backup.
        if(stored_file_matches(p,data,n,h))return true;
        if (mkdir(root, 0700) == 0 || errno == EEXIST) {
            char tmp[256], bak[256];
            snprintf(tmp, sizeof(tmp), "%s/%s.tmp", root, key);
            snprintf(bak, sizeof(bak), "%s/%s.bak", root, key);
            FILE* f = fopen(tmp, "wb");
            if (f) {
                bool ok = fwrite(h, 1, sizeof(h), f) == sizeof(h) && fwrite(data, 1, n, f) == n &&
                          fflush(f) == 0;
                if (ok && fsync(fileno(f)) != 0 && errno != ENOSYS && errno != EINVAL) ok = false;
                if (fclose(f) != 0) ok = false;
                if (ok) {
                    // 只提升已验证的数据；坏主文件不能覆盖可恢复的备份。/ Promote only a verified primary, retaining recovery from a corrupt one.
                    if (load_file(p, NULL, 0)) {
                        if (unlink(bak) != 0 && errno != ENOENT) ok = false;
                        if (ok && rename(p, bak) != 0) ok = false;
                    } else if (unlink(p) != 0 && errno != ENOENT)
                        ok = false;
                    if (ok && rename(tmp, p) == 0) return true;
                }
                int saved_errno=errno;unlink(tmp);errno=saved_errno;
            }
        }
    return false;
}
PICO_STORE_COLD bool ps_store(const char* key, const void* data, size_t n) {
    if (!good_key(key) || !data || !n || n > 65536) return false;
    if (ps_storage_busy() || s_transfer_prepared || s_external_active) {
        ps_sound_error();
        ps_toast("请先结束文件传输或存储维护，再保存笔记");return false;
    }
    if (s_store_lost) {
        ps_sound_error();
        ps_toast("存储卡已移除，内容暂留本次会话");
        return false;
    }
    if (s_store_root[0]) {
        if (store_file(s_store_root,key,data,n)) return true;
        // 有文件存储时不暗中落到更低优先级NVS，否则下次读出旧文件。/ Never report a lower-priority NVS fallback as a successful file save.
    } else if (n <= 1024 && strlen(key) <= 15) {
        nvs_handle_t h;
        if (nvs_open("pico_system", NVS_READWRITE, &h) == ESP_OK) {
            esp_err_t e = nvs_set_blob(h, key, data, n);
            if (e == ESP_OK) e = nvs_commit(h);
            nvs_close(h);
            if (e == ESP_OK) return true;
        }
    }
    ps_sound_error();
    ps_toast(!s_store_root[0] && n>1024 ? "笔记存储暂不可用，内容暂留本次会话" :
             s_store_root[0] && errno == ENOSPC ? "存储空间不足，内容暂留本次会话" :
             s_store_root[0] && errno == EROFS ? "存储只读，内容暂留本次会话" :
             "写入失败，内容仍保留在本次会话中");
    return false;
}
bool ps_storage_save_allowed(void) {
    return !ps_storage_busy()&&!s_transfer_prepared&&!s_external_active&&!s_store_lost;
}
static void checkpoint_invalidate(void) {
    ps_reader_checkpoint_invalidate();ps_home_storage_invalidate();
}
// 合并只接受当前挂载存储的规范目录；不会绕过USB与维护独占。/ Imports accept canonical mounted roots and never bypass USB or maintenance ownership.
static PICO_STORE_COLD bool record_root_allowed(const char *root) {
    if (!root || !*root || ps_storage_busy() || s_transfer_prepared || s_external_active) return false;
    book_store_root_t roots[2];int count=0;
    if (book_store_roots(roots,&count)!=ESP_OK) return false;
    for (int i=0;i<count;i++) {
        char allowed[160];int n=snprintf(allowed,sizeof(allowed),"%s/.pico-system",roots[i].path);
        if(n>0&&n<(int)sizeof(allowed)&&!strcmp(root,allowed)) return true;
    }
    return false;
}
// 阅读记录优先TF，系统设置仍使用原绑定根；另一根只供显式旧记录合并。
// Prefer TF for reading records without moving system settings; retain the other root for explicit imports.
PICO_STORE_COLD bool ps_storage_record_roots(char *current,char *other,size_t capacity,bool *current_is_sd) {
    if(!current||!other||!capacity)return false;
    current[0]=other[0]=0;
    if(s_store_lost||!record_root_allowed(s_store_root))return false;
    book_store_root_t roots[2];int count=0;
    if(book_store_roots(roots,&count)!=ESP_OK||!count)return false;
    int selected=0;
    for(int i=0;i<count;i++)if(!roots[i].is_flash){selected=i;break;}
    if(current_is_sd)*current_is_sd=!roots[selected].is_flash;
    int n=snprintf(current,capacity,"%s/.pico-system",roots[selected].path);
    if(n<0||(size_t)n>=capacity){current[0]=0;return false;}
    for(int i=0;i<count;i++)if(i!=selected){
        n=snprintf(other,capacity,"%s/.pico-system",roots[i].path);
        if(n<0||(size_t)n>=capacity){other[0]=0;return false;}
        break;
    }
    return true;
}
PICO_STORE_COLD bool ps_load_at(const char *root,const char *key,void *out,size_t n) {
    if(!good_key(key)||!out||!n||n>65536||!record_root_allowed(root))return false;
    char path[256];
    snprintf(path,sizeof(path),"%s/%s.dat",root,key);
    if(load_file(path,out,n))return true;
    snprintf(path,sizeof(path),"%s/%s.bak",root,key);
    return load_file(path,out,n);
}
bool ps_store_at(const char *root,const char *key,const void *data,size_t n) {
    return good_key(key)&&data&&n&&n<=65536&&record_root_allowed(root)&&store_file(root,key,data,n);
}
PICO_STORE_COLD bool ps_store_exists_at(const char *root,const char *key,bool *exists) {
    if(!exists||!good_key(key)||!record_root_allowed(root))return false;
    *exists=false;const char *suffix[]={"dat","bak"};
    for(int i=0;i<2;i++){
        char path[256];struct stat st;snprintf(path,sizeof(path),"%s/%s.%s",root,key,suffix[i]);
        if(stat(path,&st)==0){if(!S_ISREG(st.st_mode))return false;*exists=true;}
        else if(errno!=ENOENT)return false;
    }
    return true;
}
#include "pico_reading_snapshot.inc"
static void save_config(void) {
    ps_store("system_v1", &s_config, sizeof(s_config));
}
static int quick_canonical(int id) { return id==38?17:id==39?18:id==40?33:id; }
static int quick_catalog(int out[QUICK_COUNT]) {
    int n=0;for(int id=0;id<QUICK_COUNT;id++)if(quick_canonical(id)==id)out[n++]=id;
    return n;
}
static bool quick_enabled(int id) {
    id=quick_canonical(id);for(int i=0;i<s_shortcuts.count;i++)if(s_shortcuts.ids[i]==id)return true;
    return false;
}
static int quick_slot_of(const uint8_t slots[FAN_SLOTS],int id) {
    for(int i=0;i<FAN_SLOTS;i++)if(slots[i]==id)return i;
    return -1;
}
static int quick_placed(const uint8_t slots[FAN_SLOTS]) {
    int n=0;for(int i=0;i<FAN_SLOTS;i++)if(slots[i]<QUICK_COUNT)n++;return n;
}
static PICO_STORE_COLD void quick_seed_slots(void) {
    static const uint8_t roots[]={0,4,11};
    memset(s_quick_slots,QUICK_EMPTY,sizeof(s_quick_slots));int n=0;
    for(int i=0;i<ARRAY_COUNT(roots);i++)s_quick_slots[n++]=roots[i];
    for(int i=0;i<s_shortcuts.count&&n<FAN_SLOTS;i++)
        if(quick_slot_of(s_quick_slots,s_shortcuts.ids[i])<0)s_quick_slots[n++]=s_shortcuts.ids[i];
    // 旧内圈一直可见，迁移后仍保留这些入口；超出14项的旧动作仅作未放置备选。
    // Legacy roots were always visible; retain them and keep excess enabled actions as unplaced alternatives.
    for(int i=0;i<ARRAY_COUNT(roots);i++)if(!quick_enabled(roots[i])&&s_shortcuts.count<QUICK_MAX)s_shortcuts.ids[s_shortcuts.count++]=roots[i];
}
static PICO_STORE_COLD void quick_import(const uint8_t *ids,int count) {
    memset(&s_shortcuts,0,sizeof(s_shortcuts));s_shortcuts.version=4;
    memset(s_shortcuts.order,255,sizeof(s_shortcuts.order));
    for(int i=0;i<count&&s_shortcuts.count<QUICK_MAX;i++) {
        int old=ids[i];if(old>=QUICK_COUNT)continue;
        int id=quick_canonical(old),group=s_quick[old].group;
        if(!quick_enabled(id))s_shortcuts.ids[s_shortcuts.count++]=id;
        s_shortcuts.group_mask[id]|=1u<<group;
        if(s_shortcuts.order[group][id]==255)s_shortcuts.order[group][id]=i;
    }
    quick_seed_slots();
}
static void quick_defaults(void) {
    quick_import(s_quick_defaults,FAN_SLOTS);
    memcpy(s_quick_slots,s_quick_defaults,sizeof(s_quick_slots));
}
static PICO_STORE_COLD void quick_adopt(const quick_saved_t *saved) {
    memset(&s_shortcuts,0,sizeof(s_shortcuts));s_shortcuts.version=5;
    memset(s_quick_slots,QUICK_EMPTY,sizeof(s_quick_slots));
    for(int i=0;i<saved->count;i++) {
        int id=saved->ids[i];if(id>=QUICK_COUNT)continue;id=quick_canonical(id);
        if(!quick_enabled(id))s_shortcuts.ids[s_shortcuts.count++]=id;
    }
    for(int i=0;i<FAN_SLOTS;i++) {
        int id=saved->slots[i];if(id>=QUICK_COUNT)continue;id=quick_canonical(id);
        if(quick_slot_of(s_quick_slots,id)>=0)continue;
        s_quick_slots[i]=id;
        if(!quick_enabled(id)&&s_shortcuts.count<QUICK_MAX)s_shortcuts.ids[s_shortcuts.count++]=id;
    }
}
static PICO_STORE_COLD void quick_load(void) {
    quick_saved_t v5;
    if(ps_load("quick_v5",&v5,sizeof(v5))&&v5.version==5&&v5.count<=QUICK_MAX){quick_adopt(&v5);return;}
    // 旧布局只有40个启用槽；新增“贴合防误触”后按原顺序迁移，不丢用户编排。
    // The previous layout held 40 enabled slots; migrate the new guard action without losing layout.
    quick_saved_v5_40_t old5;
    if(ps_load("quick_v5",&old5,sizeof(old5))&&old5.version==5&&old5.count<=40){
        quick_saved_t converted={.version=5};
        memcpy(converted.slots,old5.slots,sizeof(converted.slots));
        converted.count=old5.count;
        memcpy(converted.ids,old5.ids,old5.count);
        quick_adopt(&converted);return;
    }
    if(ps_load("quick_v4",&s_shortcuts,sizeof(s_shortcuts))&&s_shortcuts.version==4&&s_shortcuts.count<=QUICK_MAX) {
        uint8_t old[QUICK_MAX];int count=s_shortcuts.count;memcpy(old,s_shortcuts.ids,count);quick_import(old,count);return;
    }
    struct {uint32_t version;uint8_t count,ids[40];} v3;
    if(ps_load("quick_v3",&v3,sizeof(v3))&&v3.version==3&&v3.count<=40){quick_import(v3.ids,v3.count);return;}
    struct {uint32_t version;uint8_t count,ids[12];} v2;
    if(!ps_load("quick_v2",&v2,sizeof(v2))||v2.version!=2||v2.count>12){quick_defaults();return;}
    static const uint8_t defaults[]={0,13,7,9,14};
    if(v2.count==sizeof(defaults)&&!memcmp(v2.ids,defaults,sizeof(defaults))){quick_defaults();return;}
    quick_import(v2.ids,v2.count);
}
static PICO_STORE_COLD bool quick_save_slots(const uint8_t slots[FAN_SLOTS]) {
    quick_saved_t saved={.version=5};memcpy(saved.slots,slots,sizeof(saved.slots));
    uint64_t seen=0;
    for(int i=0;i<s_shortcuts.count+FAN_SLOTS;i++) {
        int id=i<s_shortcuts.count?s_shortcuts.ids[i]:slots[i-s_shortcuts.count];
        if(id>=QUICK_COUNT)continue;
        id=quick_canonical(id);
        if(!(seen&(UINT64_C(1)<<id))){seen|=UINT64_C(1)<<id;saved.ids[saved.count++]=id;}
    }
    if(!ps_store("quick_v5",&saved,sizeof(saved)))return false;
    quick_adopt(&saved);return true;
}
static void quick_edit_contact_cancel(void) {
    s_quick_edit.source=s_quick_edit.target=-1;s_quick_edit.dragging=false;s_quick_edit.suppress_tap=true;
}
static void quick_edit_begin(void) {
    memset(&s_quick_edit,0,sizeof(s_quick_edit));s_quick_edit.active=true;
    s_quick_edit.selected=s_quick_edit.source=s_quick_edit.target=-1;
    memcpy(s_quick_edit.slots,s_quick_slots,sizeof(s_quick_slots));s_quick_scroll=s_quick_mode=0;
}
static void quick_edit_snapshot(void) {
    memcpy(s_quick_edit.undo,s_quick_edit.slots,FAN_SLOTS);s_quick_edit.undo_valid=true;s_quick_edit.save_failed=false;
}
static PICO_STORE_COLD void quick_edit_assign(int id) {
    int at=s_quick_edit.selected;if(at<0||at>=FAN_SLOTS)return;
    if(id<0||id>=QUICK_COUNT)id=QUICK_EMPTY;else id=quick_canonical(id);
    if(s_quick_edit.slots[at]==id)return;
    quick_edit_snapshot();int other=id==QUICK_EMPTY?-1:quick_slot_of(s_quick_edit.slots,id);
    if(other>=0)s_quick_edit.slots[other]=s_quick_edit.slots[at];
    s_quick_edit.slots[at]=id;
}
static book_store_root_t s_transfer_root;
static uint64_t s_transfer_free;
static size_t s_transfer_limit;
static uint64_t transfer_free_bytes(void *ctx) { return book_store_free_bytes(ctx); }
bool ps_transfer_prepared(void) { return s_transfer_prepared; }
PICO_STORE_COLD esp_err_t ps_transfer_prepare(read_pico_transfer_cfg_t *out) {
    if (!out) return ESP_ERR_INVALID_ARG;
    if (ps_storage_busy() || s_transfer_prepared || s_external_active || !ps_weread_pause()) return ESP_ERR_INVALID_STATE;
    if (!ps_games_prepare_storage()) { ps_weread_resume(); return ESP_ERR_INVALID_STATE; }
    esp_err_t err = ttf_font_suspend_sd(true);
    if (err != ESP_OK) { ttf_font_suspend_sd(false); ps_weread_resume(); return err; }
    err = book_store_upload_root(&s_transfer_root);
    if (err != ESP_OK) { ttf_font_suspend_sd(false); ps_weread_resume(); return err; }
    s_transfer_free = book_store_free_bytes(&s_transfer_root);
    s_transfer_limit = book_store_file_limit(&s_transfer_root);
    // 满卡仍可开启文件管理并删除旧书；接收端继续逐请求检查容量。/ A full card can still serve management/deletion; reception checks capacity per request.
    s_transfer_prepared = true;
    display_set_bulk_io(true);
    *out = (read_pico_transfer_cfg_t){
        .root_dir = s_transfer_root.path,
        .font_dir = s_transfer_root.is_flash ? NULL : "/sdcard/fonts",
        .is_flash = s_transfer_root.is_flash,
        .file_limit = s_transfer_limit,
        .free_bytes_cb = transfer_free_bytes,
        .free_bytes_ctx = &s_transfer_root,
        .file_changed_cb = book_progress_forget,
    };
    return ESP_OK;
}
void ps_transfer_capacity(uint64_t *free_bytes,bool *is_flash,size_t *file_limit) {
    // 上传工作线程也会查询FAT容量，界面只复用准备时的值。/ Reuse the prepared capacity; the upload worker queries FAT independently.
    if (free_bytes) *free_bytes = s_transfer_prepared ? s_transfer_free : 0;
    if (is_flash) *is_flash = s_transfer_prepared && s_transfer_root.is_flash;
    if (file_limit) *file_limit = s_transfer_prepared ? s_transfer_limit : 0;
}
PICO_STORE_COLD void ps_transfer_finished(unsigned changed_count) {
    if (!s_transfer_prepared) return;
    // 调用方已确认接收任务join；此后才允许界面落盘与重新打开卡上字体。/ Caller has confirmed reception joined before UI persistence or card-font reopening.
    s_transfer_prepared = false;
    checkpoint_invalidate();
    display_set_bulk_io(false);
    ttf_font_suspend_sd(false);
    if (changed_count) { book_store_notify_changed(); ps_games_rescan(); }
    if (!s_store_lost) save_config();
    memset(&s_transfer_root,0,sizeof(s_transfer_root));
    s_transfer_free = s_transfer_limit = 0;
    ps_weread_resume();
}
const char* ps_storage_target(void) {
    if (s_store_lost) return "笔记存储：TF 卡暂不可用";
    if (s_store_root[0]) return s_store_is_sd ? "笔记存储：TF 卡" : "笔记存储：内置存储";
    return "笔记存储：暂不可用；小设置保存在设备内";
}
static void store_media_bind(void) {
    read_pico_sd_info_t card={0};
    s_store_media_known=s_store_is_sd&&read_pico_sd_get_info(&card)==ESP_OK&&card.present&&card.mounted&&!card.exported;
    if(s_store_media_known)s_store_media_generation=card.media_generation;
}
void ps_media_restored(void) {
    if(!s_ready||s_external_active||s_transfer_prepared||ps_storage_busy()||ps_usb_held())return;
    read_pico_sd_info_t card={0};
    if(read_pico_sd_get_info(&card)!=ESP_OK||!card.present||!card.mounted||card.exported)return;
    checkpoint_invalidate();
    // 只恢复同一存储会话的写入资格，不把旧卡RAM配置写入新卡。
    // Restore writes only for the bound session; never publish the previous card's RAM onto a replacement.
    if(s_store_is_sd&&s_store_media_known&&card.media_generation==s_store_media_generation)s_store_lost=false;
    ttf_font_suspend_sd(false);
    book_store_notify_changed();
    ps_games_rescan();
    s_toast[0]=0;
    s_dirty=true;
}
PICO_STORE_COLD bool ps_storage_prepare(void) {
    ps_dual_suspend();
    if (s_external_active || !ps_weread_pause()) return false;
    if (!ps_games_prepare_storage()) { ps_weread_resume(); return false; }
    if (!ps_connectivity_shutdown()) { s_network_resume_pending = true; ps_weread_resume(); return false; }
    s_network_resume_pending = false;
    checkpoint_invalidate();
    // 维护期间禁止后台落盘；阅读与字体先退回内置资源。/ Block persistence during maintenance and detach card fonts first.
    bool was_lost = s_store_lost;
    if (s_store_is_sd) s_store_lost = true;
    ps_reader_media_lost();
    if (ttf_font_suspend_sd(true) != ESP_OK) {
        ttf_font_suspend_sd(false);
        s_network_resume_pending = read_pico_connectivity_resume() != ESP_OK;
        s_store_lost = was_lost;
        ps_weread_resume();
        return false;
    }
    s_tracking = s_fan = s_fan_candidate = false;
    s_toast[0] = 0;
    return true;
}
PICO_STORE_COLD bool ps_storage_rebind(bool formatted) {
    checkpoint_invalidate();
    // 已有卡数据不以当前RAM默认值覆盖；新卡或明确清空后才迁移本次会话。/ Preserve existing card data; migrate RAM only to a new or explicitly formatted store.
    if (s_store_root[0] && !s_store_is_sd && !s_store_lost) return true;
    if(!formatted&&s_store_is_sd&&s_store_media_known){
        read_pico_sd_info_t card={0};
        if(read_pico_sd_get_info(&card)==ESP_OK&&card.present&&card.mounted&&!card.exported&&card.media_generation==s_store_media_generation){
            s_store_lost=false;
            return true;
        }
        // 已绑定的会话不能借“新卡没有目录”迁移旧RAM；换卡后须重新读取该卡。
        // A bound session must not migrate old RAM merely because a replacement lacks a directory; reload that card instead.
        s_store_lost=true;
        return false;
    }
    book_store_root_t roots[2];
    int count = 0;
    if (book_store_roots(roots, &count) != ESP_OK) return false;
    char target[160] = {0};
    for (int i = 0; i < count; i++) if (!roots[i].is_flash) {
        snprintf(target, sizeof(target), "%s/.pico-system", roots[i].path);
        break;
    }
    if (!target[0]) return false;
    struct stat st;
    if (!formatted) {
        if (stat(target, &st) == 0) {
            ps_toast("卡内已有系统目录，请重启读取；未覆盖原数据");
            return false;
        }
        if (errno != ENOENT) return false;
    }
    if (mkdir(target, 0700) != 0 && (!formatted || errno != EEXIST)) return false;
    snprintf(s_store_root, sizeof(s_store_root), "%s", target);
    s_store_is_sd = true;
    s_store_lost = false;
    store_media_bind();
    bool ok = ps_store("system_v1", &s_config, sizeof(s_config));
    if (!ps_reader_storage_save()) ok = false;
    if (!ps_home_storage_save()) ok = false;
    if (!ps_navigation_storage_save()) ok = false;
    if (!ps_games_storage_save()) ok = false;
    ps_toast(ok ? "笔记与设置存储已启用" : "部分数据未保存，请保持开机，重新保存未保存的笔记");
    return ok;
}
void ps_storage_resume(void) {
    checkpoint_invalidate();
    ttf_font_suspend_sd(false);
    s_network_resume_pending = read_pico_connectivity_resume() != ESP_OK;
    ps_weread_resume();
    ps_media_restored();
}
PICO_STORE_COLD bool ps_external_prepare(void) {
    if (s_external_active || ps_storage_busy() || s_transfer_prepared) return false;
    read_pico_sd_info_t before={0};
    if (read_pico_sd_get_info(&before)!=ESP_OK || !before.mounted || before.exported) return false;
    if(!ps_weread_pause())return false;
    bool was_lost=s_store_lost;
    // 在卡交给电脑前生成两处已提交笔记的只读快照；失败不暴露过时快照。/ Snapshot both committed note stores before host handoff; failure never exposes stale snapshots.
    if(!ps_connectivity_shutdown()){s_network_resume_pending=true;ps_weread_resume();return false;}
    if(!ps_reader_power_save()||!reading_snapshot_prepare()){
        s_network_resume_pending=read_pico_connectivity_resume()!=ESP_OK;
        ps_weread_resume();
        ps_toast("阅读记录同步准备失败，未开启 USB；原笔记保留");return false;
    }
    if (!ps_storage_prepare()) { ps_weread_resume(); return false; }
    // 维护已持有自己的租约；释放外层USB准备租约，由external_finished归还剩下的一份。
    // Storage preparation owns its lease; release the outer USB lease, retaining one until external_finished.
    ps_weread_resume();
    read_pico_sd_info_t after={0};
    if (read_pico_sd_get_info(&after)!=ESP_OK || !after.mounted || after.exported || after.media_generation!=before.media_generation) {
        s_store_lost=s_store_is_sd?true:was_lost;
        ps_storage_resume();return false;
    }
    s_external_was_lost=was_lost;s_external_generation=after.media_generation;
    s_external_active=true;display_set_bulk_io(true);return true;
}
PICO_STORE_COLD bool ps_external_finished(bool changed) {
    if (!s_external_active) return false;
    read_pico_sd_info_t info={0};
    esp_err_t error=read_pico_sd_get_info(&info);
    if (info.exported) return false;
    bool ready=error==ESP_OK && info.mounted;
    bool same=ready && info.media_generation==s_external_generation;
    s_external_active=false;
    if (s_store_is_sd) s_store_lost=same?s_external_was_lost:true;
    display_set_bulk_io(false);
    // USB结束前没有本地文件访问；验证后再恢复字体、文件书架与设置。
    // No local file access occurs while exported; restore fonts, library and persistence after verification.
    ps_storage_resume();
    if (changed || !same) { book_store_notify_changed(); ps_games_rescan(); }
    if(ready)ps_desktop_usb_reclaimed();
    // A replacement card must never receive the previous card's RAM notes.
    // 换卡后不把旧卡的会话笔记迁移到新卡；仅同一张卡可重新绑定空存储。
    if (same && !s_store_root[0]) (void)ps_storage_rebind(false);
    if (same && !s_store_lost) save_config();
    return ready;
}
bool ps_content_held(void){return s_content_owned;}
// HTTP线程仅申请/归还租约；所有保存、字体和文件消费者切换均由UI线程执行。
// HTTP only requests/releases a lease; the UI thread alone saves and pauses fonts/file consumers.
static bool content_acquire(void){
    bool requested=false;
    for(unsigned i=0;i<300;i++){
        unsigned state=atomic_load(&s_content_state);
        if(!requested&&state==CONTENT_IDLE){unsigned expected=CONTENT_IDLE;requested=atomic_compare_exchange_strong(&s_content_state,&expected,CONTENT_REQUEST);}
        else if(requested){if(state==CONTENT_GRANTED)return true;if(state!=CONTENT_REQUEST)return false;}
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    if(!requested)return false;
    unsigned expected=CONTENT_REQUEST;
    return !atomic_compare_exchange_strong(&s_content_state,&expected,CONTENT_IDLE)&&expected==CONTENT_GRANTED;
}
static void content_release(bool changed){atomic_store(&s_content_state,changed?CONTENT_CHANGED:CONTENT_RELEASED);}
static void content_cancel(void){unsigned expected=CONTENT_REQUEST;(void)atomic_compare_exchange_strong(&s_content_state,&expected,CONTENT_IDLE);}
static bool content_tick(void){
    unsigned state=atomic_load(&s_content_state);
    if(s_content_owned&&(state==CONTENT_RELEASED||state==CONTENT_CHANGED)){
        bool changed=state==CONTENT_CHANGED,refresh=s_content_refresh;
        if(changed){ps_reader_media_lost();s_toast[0]=0;}
        bool ready=ps_external_finished(changed);
        if(s_external_active)return false;
        s_content_owned=false;
        atomic_store(&s_content_state,CONTENT_IDLE);
        return changed||refresh||!ready;
    }
    if(state!=CONTENT_REQUEST||!ps_background_io_allowed())return false;
    read_pico_sd_info_t card={0};
    bool ready=!read_pico_sd_get_info(&card)&&card.present&&card.mounted&&!card.exported;
    if(!ready||ps_storage_busy()||s_transfer_prepared||s_external_active||ps_usb_held()||!ps_weread_pause()){
        content_cancel();return false;
    }
    if((s_global_app==&app_book&&!app_book_power_prepare())||!ps_reader_power_save()||!ps_games_prepare_storage()){
        ps_weread_resume();content_cancel();return false;
    }
    if(ttf_font_suspend_sd(true)!=ESP_OK){ttf_font_suspend_sd(false);ps_weread_resume();content_cancel();return false;}
    read_pico_sd_info_t checked={0};
    if(read_pico_sd_get_info(&checked)!=ESP_OK||!checked.present||!checked.mounted||checked.exported||checked.media_generation!=card.media_generation){
        if(s_store_is_sd)s_store_lost=true;
        ttf_font_suspend_sd(false);ps_weread_resume();content_cancel();return false;
    }
    s_external_was_lost=s_store_lost;s_external_generation=card.media_generation;
    s_external_active=s_content_owned=true;s_content_refresh=false;display_set_bulk_io(true);
    unsigned expected=CONTENT_REQUEST;
    if(!atomic_compare_exchange_strong(&s_content_state,&expected,CONTENT_GRANTED)){
        (void)ps_external_finished(false);
        // 授权前取消也必须等待外部持卡者归还，不能提前恢复本机文件访问。
        // Cancellation before grant still waits for an external card owner before restoring local access.
        if(s_external_active)atomic_store(&s_content_state,CONTENT_RELEASED);
        else s_content_owned=false;
    }
    return false;
}
static bool local_hold(void){return ps_usb_background()||s_content_owned;}
static bool local_before_leave(ps_route_t target){
    if(s_content_owned&&!ps_usb_local_route(target)){ps_toast("内容正在同步，请稍后再试");return true;}
    return ps_usb_before_leave(target);
}
ps_view_t ps_view(void) {
    bool landscape = s_config.landscape;
    if (s_global_app || ps_dual_enabled()) {
        // 子页可使用不同方向，覆盖层跟随实际画布。/ Follow the actual child canvas orientation.
        enum EpdRotation rotation = epd_get_rotation();
        landscape = rotation == EPD_ROT_LANDSCAPE || rotation == EPD_ROT_INVERTED_LANDSCAPE;
    }
    return landscape ? (ps_view_t){ 640, 360, true } : (ps_view_t){ 360, 640, false };
}
ps_route_t ps_route(void) {
    return s_route;
}
ps_route_t ps_previous_route(void) {
    return s_depth ? s_history[s_depth - 1] : PS_SHELF;
}
int64_t ps_now(void) {
    return s_ctx ? s_ctx->now_ms : 0;
}
unsigned ps_background_io_blockers(void) {
    return (s_external_active?1u:0u)|(ps_usb_held()?2u:0u)|(s_fan?4u:0u)|
        (s_fan_candidate?8u:0u)|(s_panel?16u:0u)|(s_quick_edit.active?32u:0u)|
        (s_tracking?64u:0u)|(s_center_down?128u:0u)|(s_center_pending?256u:0u)|
        (s_ctx&&(s_ctx->request_app||s_ctx->request_return)?512u:0u)|
        (s_ctx&&s_ctx->touch&&s_ctx->touch->touched?1024u:0u);
}
bool ps_background_io_allowed(void) {
    return ps_background_io_blockers()==0;
}
void ps_dirty(void) {
    s_dirty = true;
}
void ps_toast(const char* text) {
    snprintf(s_toast, sizeof(s_toast), "%s", text ? text : "");
    s_toast_until = ps_now() + 3000;
    s_dirty = true;
}
void ps_notice(const char* text) {
    snprintf(s_notice, sizeof(s_notice), "%s", text ? text : "");
    s_notice_until = ps_now() + 3000;
    ps_dirty();
}
void ps_notice_draw(uint8_t* fb, int y) {
    if (!fb || !s_notice[0] || ps_now() >= s_notice_until) return;
    ps_center(fb, ps_view().w / 2, y, 10, s_notice, 5);
}
static bool reader_route(ps_route_t r) {
    return r == PS_SHELF || r == PS_READ || r == PS_RECORDS || r == PS_READING_SETTINGS ||
           r == PS_INPUT_SETTINGS || r == PS_IMPORT || r == PS_EDITOR;
}
static bool home_route(ps_route_t r) {
    return r == PS_HA || r == PS_PRINTERS || r == PS_CLOCK || r == PS_QUOTES ||
           r == PS_HA_SETTINGS || r == PS_PRINTER_SETTINGS || r == PS_SLEEP_SETTINGS;
}
static bool setting_route(ps_route_t r) {
    return r == PS_DEVICE || r == PS_WIFI || r == PS_TRANSFER || r == PS_WIRELESS_MANAGE || r == PS_HA_SETTINGS || r == PS_PRINTER_SETTINGS ||
           r == PS_READING_SETTINGS || r == PS_INPUT_SETTINGS || r == PS_SLEEP_SETTINGS ||
           r == PS_QUICK_SETTINGS || r == PS_NAV_SETTINGS || r == PS_STORAGE || r == PS_USB_TRANSFER || r == PS_BLUETOOTH || r == PS_DATETIME || r == PS_POWER || r == PS_SOUND || r == PS_DUAL || r == PS_ANIMATION_SETTINGS;
}
static void enter(ps_route_t r) {
    if (r == PS_GAMES || r == PS_PLAYGAME || r == PS_ARCADEGAME || r == PS_APPLICATION) s_section = 2;
    else if (r == PS_HA || r == PS_PRINTERS) s_section = 1;
    else if (r == PS_SHELF || r == PS_READ || r == PS_RECORDS) s_section = 0;
    ps_global_cancel_input();
    if (r == PS_CLOCK || r == PS_QUOTES) s_force_full=true;
    s_tracking = s_fan = s_fan_candidate = false;
    s_long = false;
    s_fan_selected = -1;
    if(r!=PS_QUICK_SETTINGS)s_quick_edit.active=false;
    else if(!s_quick_edit.active)quick_edit_begin();
    s_update_confirm=0;
    s_route = r;
    s_panel = 0;
    s_focus = 0;
    if (r == PS_STORAGE) ps_storage_enter();
    if (r == PS_USB_TRANSFER) ps_usb_enter();
    if (r == PS_BLUETOOTH) ps_bluetooth_enter();
    if (r == PS_DATETIME) ps_datetime_enter();
    if (r == PS_POWER) ps_power_enter();
    if (r == PS_DUAL) ps_dual_enter();
    if (r == PS_NAV_SETTINGS) ps_navigation_enter();
    ps_connectivity_enter(r);
    ps_reader_enter(r);
    ps_home_enter(r);
    ps_games_enter(r);
    if(s_route==r&&r==PS_APPS)ps_launcher_enter();
    if(s_route==r&&(r==PS_CANVAS||r==PS_CLOUDFLARE))ps_desktop_prepare_app(r==PS_CLOUDFLARE);
    s_dirty = true;
    ESP_LOGI(
        "pico_system", "route=%d orientation=%s", r, s_config.landscape ? "landscape" : "portrait");
}
static bool resume_real_book(void) {
    if ((!s_real_book_preferred && !ps_dual_has_reading()) || s_transfer_prepared || s_external_active || ps_storage_busy() || ps_usb_held()) return false;
    if (!ps_games_prepare_storage()) return false;
    if (!app_book_resume_last()) return false;
    s_book_return = false;
    ps_legacy(&app_book);
    return true;
}
bool ps_global_resume_reading(void) { return resume_real_book(); }
void ps_global_book_return(bool enabled) { s_book_return = enabled; }
void ps_global_prefer_real_book(bool enabled) {
    s_real_book_preferred = enabled;
    if (!enabled) s_book_return = false;
}
static void global_depart(void) {
    if (!s_global_app || !s_ctx) return;
    if (s_global_app == &app_book && !s_external_active && !ps_usb_held()) {
        s_book_return = true;
        s_book_return_depth = s_depth;
        app_book_global_prepare();
    }
    s_ctx->request_app = &app_pico_system;
}
void ps_navigate(ps_route_t r) {
    if(r>=0&&r<PS_ROUTE_COUNT&&local_before_leave(r))return;
    if(r==PS_CLOCK||r==PS_QUOTES){ps_power_preview(r==PS_QUOTES?1:0);return;}
    if (s_global_app && r == PS_READING_SETTINGS && s_global_app == &app_book) {
        s_global_redraw = app_book_global_action(s_ctx, BOOK_GLOBAL_FONT);
        return;
    }
    // 本机阅读与示例排版存档独立，不能让系统设置看似修改了真实图书。/ Real-book and sample typography are separate; never imply these preferences affect a real book.
    if (r == PS_READING_SETTINGS && s_book_return && s_real_book_preferred) {
        ps_toast("请返回图书，从快捷菜单调整阅读排版");
        return;
    }
    if (r >= 0 && r < PS_ROUTE_COUNT && r != s_route && ps_connectivity_before_leave(r)) return;
    if (r >= 0 && r < PS_ROUTE_COUNT && (r != s_route || s_global_app)) global_depart();
    if (r == PS_DUAL || r == PS_SHELF) s_book_return = false;
    // 双机设置页属于系统壳，不能继续由阅读页吞掉角色/方向触摸。
    // Dual settings belong to the system shell and must not let the reader swallow role/orientation touches.
    if (r == PS_DUAL && s_ctx) s_ctx->request_app = &app_pico_system;
    if (s_route == PS_SHELF || s_route == PS_READ || s_route == PS_RECORDS)
        s_workspace[0] = s_route;
    if (s_route == PS_HA || s_route == PS_PRINTERS) s_workspace[1] = s_route;
    if (r < 0 || r >= PS_ROUTE_COUNT) return;
    if (r == PS_DEVICE) {
        bool app_return = false;
        if (s_route == PS_APPLICATION)
            for (int i = s_depth - 1; i >= 0; i--)
                if (s_history[i] == PS_DEVICE) { app_return = true; break; }
        if (!app_return) s_device_page = 0;
    }
    if (r == s_route) {
        s_panel = 0;
        s_dirty = true;
        return;
    }
    if (r == PS_CLOCK || r == PS_QUOTES) {
        s_sleep_origin = s_route;
        enter(r);
        return;
    }
    for (int i = s_depth - 1; i >= 0; i--)
        if (s_history[i] == r) {
            s_depth = i;
            enter(r);
            return;
        }
    if (s_depth == ARRAY_COUNT(s_history)) {
        memmove(s_history, s_history + 1, sizeof(s_history[0]) * (ARRAY_COUNT(s_history) - 1));
        s_depth--;
    }
    s_history[s_depth++] = s_route;
    enter(r);
}
// 自定义顺序只用于启动器；旧快捷动作与中键长按仍按固定目的地跳转。/ Custom order is launcher-only; shortcuts and center hold retain fixed destinations.
static void launcher_switch(int index) {
    const ps_navigation_entry_t *item=ps_navigation_item(index);
    if(!item)return;
    if(local_before_leave(item->route)||ps_connectivity_before_leave(item->route))return;
    if(item->launch){global_depart();s_depth=0;s_book_return=false;item->launch(item->id);return;}
    global_depart();s_depth=0;s_book_return=false;enter(item->route);
}
static bool launcher_root(void) {
    if(s_depth&&s_history[s_depth-1]==PS_DEVICE&&(s_route==PS_APPS||s_route==PS_GAMES))return false;
    return s_route==PS_SHELF||s_route==PS_GAMES||(s_route==PS_APPS&&ps_navigation_index(PS_APPS)>=0)||ps_home_launcher_visible();
}
void ps_switch(int section) {
    if(section<0||section>2)return;
    static const ps_route_t roots[]={PS_SHELF,PS_HA,PS_GAMES};
    ps_route_t target=roots[section];
    if(local_before_leave(target)||ps_connectivity_before_leave(target))return;
    global_depart();
    // 根页切换不恢复更深页面，阅读和房间状态留在各模块。/ Root switches retain module state, never a child route.
    s_depth=0;s_section=section;s_book_return=false;enter(target);
}
void ps_back(void) {
    if (ps_storage_busy()) return;
    if(s_route==PS_APPS&&ps_launcher_back())return;
    if(s_route==PS_DEVICE&&s_update_confirm){s_update_confirm=0;s_dirty=true;return;}
    if (s_route == PS_POWER && ps_power_back()) { s_dirty=true; return; }
    if ((s_route == PS_USB_TRANSFER || ps_usb_background()) && ps_usb_back()) { s_dirty=true; return; }
    if (s_route == PS_STORAGE && ps_storage_back()) { s_dirty = true; return; }
    if (s_fan || s_fan_candidate) {
        s_fan = s_fan_candidate = s_tracking = false;
        s_dirty = true;
        return;
    }
    if(s_route==PS_QUICK_SETTINGS&&s_quick_edit.active) {
        quick_edit_contact_cancel();
        if(s_quick_mode){s_quick_mode=0;s_dirty=true;return;}
        s_quick_edit.active=false;
    }
    if(s_route==PS_NAV_SETTINGS&&ps_navigation_back()){s_dirty=true;return;}
    if (s_panel) {
        if (s_panel == 2) {
            s_panel = 1;
            s_group = 0;
        } else
            s_panel = 0;
        s_dirty = true;
        return;
    }
    if(!s_global_app&&s_route==PS_DEVICE&&s_status_style_picker){s_status_style_picker=false;s_dirty=true;return;}
    if(!s_global_app&&s_route==PS_DEVICE&&s_shelf_card_picker){s_shelf_card_picker=false;s_dirty=true;return;}
    if(!s_global_app&&s_route==PS_DEVICE&&s_device_page){s_device_page=0;s_dirty=true;return;}
    if(local_hold()){
        while(s_depth){ps_route_t p=s_history[--s_depth];if(ps_usb_local_route(p)){global_depart();enter(p);return;}}
        if(s_route!=PS_DEVICE)ps_navigate(PS_DEVICE);
        return;
    }
    if (s_global_app) {
        if (s_global_app == &app_book) s_global_redraw = app_book_global_action(s_ctx, BOOK_GLOBAL_BACK);
        else if (s_ctx) s_ctx->request_return = true;
        return;
    }
    if (s_route == PS_CLOCK || s_route == PS_QUOTES) {
        ps_wake();
        return;
    }
    if ((s_route == PS_GAMES || s_route == PS_PLAYGAME || s_route == PS_ARCADEGAME || s_route == PS_APPLICATION) && ps_games_key(-1)) {
        s_dirty = true;
        return;
    }
    if (reader_route(s_route) && ps_reader_key(-1)) {
        s_dirty = true;
        return;
    }
    if (home_route(s_route) && ps_home_key(-1)) {
        s_dirty = true;
        return;
    }
    if ((s_route == PS_WIFI || s_route == PS_TRANSFER || s_route == PS_WIRELESS_MANAGE) && ps_connectivity_back()) { s_dirty = true; return; }
    if (s_book_return && s_route != PS_DUAL && s_route != PS_SHELF &&
        s_depth <= s_book_return_depth + 1 && resume_real_book()) return;
    if (s_route == PS_ONBOARDING && s_config.onboard_step) {
        s_config.onboard_step--;
        save_config();
        s_dirty = true;
        return;
    }
    if (s_depth) {
        ps_route_t p = s_history[--s_depth];
        enter(p);
        return;
    }
    if (s_route != PS_SHELF && s_route != PS_GAMES && s_route != PS_HA) {
        enter(s_section == 2 ? PS_GAMES : s_section == 1 ? PS_HA : PS_SHELF);
        return;
    }
    if (s_section != 0)
        ps_switch(0);
    else
        ps_toast("已在书库");
}
void ps_sleep(void) { ps_power_request(PS_POWER_LOCK); }
void ps_wake(void) { ps_power_wake(); }
static void apply_rotation(enum EpdRotation rotation) {
    if (ps_dual_enabled() && rotation != EPD_ROT_PORTRAIT && rotation != EPD_ROT_INVERTED_PORTRAIT) return;
    if (s_global_app == &app_book) app_book_global_prepare();
    ps_global_cancel_input();
    if (!ps_dual_enabled()) {
        s_config.landscape = rotation == EPD_ROT_LANDSCAPE || rotation == EPD_ROT_INVERTED_LANDSCAPE;
        s_display_rotation = rotation;
        save_config();
    }
    epd_set_rotation(rotation);
    s_panel = 0;
    s_tracking = s_fan = false;
    s_orientation_candidate = -1;
    s_orientation_matches = 0;
    if (!s_global_app) {
        ps_reader_enter(s_route);
        ps_home_enter(s_route);
        ps_games_enter(s_route);
    }
    s_dirty = true;
}
static int16_t s_face_x, s_face_y, s_face_z;
static int64_t s_face_at;
static bool contact_read(bool force) {
    int64_t now = s_ctx ? s_ctx->now_ms : 0;
    if (!force && s_face_at && now - s_face_at < 200) return true;
    if (!s_ctx || !s_ctx->acc) return false;
    sc7a20h_sample_t sample;
    if (read_pico_accel_read(s_ctx->acc, &sample) != ESP_OK) return false;
    s_face_x = sample.x_mg; s_face_y = sample.y_mg; s_face_z = sample.z_mg; s_face_at = now;
    return true;
}
int ps_contact_face(void) {
    int16_t x, y, z;
    if (!ps_contact_cached(&x, &y, &z)) return 0;
    return z > 650 ? 1 : z < -650 ? -1 : 0;
}
bool ps_contact_cached(int16_t* x, int16_t* y, int16_t* z) {
    if (!x || !y || !z || !contact_read(false)) return false;
    *x = s_face_x; *y = s_face_y; *z = s_face_z;
    return true;
}
bool ps_contact_vector(int16_t* x, int16_t* y, int16_t* z) {
    if (!x || !y || !z || !s_ctx || !s_ctx->acc) return false;
    int32_t sx = 0, sy = 0, sz = 0;
    int n = 0;
    for (int i = 0; i < 8; ++i) {
        sc7a20h_sample_t sample;
        if (read_pico_accel_read(s_ctx->acc, &sample) == ESP_OK) {
            sx += sample.x_mg; sy += sample.y_mg; sz += sample.z_mg; ++n;
        }
        vTaskDelay(pdMS_TO_TICKS(12));
    }
    if (n < 4) return false;
    *x = (int16_t)(sx / n); *y = (int16_t)(sy / n); *z = (int16_t)(sz / n);
    return true;
}

void ps_dual_rotation_changed(void) {
    // 游戏页固定横屏，不被双机阅读方向覆盖；离开游戏后自动恢复。
    // Game pages own a fixed landscape rotation; leaving the game restores dual-reading rotation.
    if (s_route == PS_ARCADEGAME || s_route == PS_APPLICATION) return;
    bool manual = ps_dual_take_manual_rotation();
    // 从机锁定后不再跟随主机基准；本机手动翻转仍通过一次性标记放行。
    // A locked secondary stops following the primary base; its own manual flip passes once.
    if (ps_dual_enabled() && ps_dual_role() == 2 && ps_dual_rotation_locked() && !manual &&
        (epd_get_rotation() == EPD_ROT_PORTRAIT || epd_get_rotation() == EPD_ROT_INVERTED_PORTRAIT))
        return;
    int rotation = ps_dual_enabled() ? ps_dual_rotation() : s_display_rotation;
    if (rotation < 0) rotation = s_config.landscape ? EPD_ROT_INVERTED_LANDSCAPE : EPD_ROT_INVERTED_PORTRAIT;
    if ((int)epd_get_rotation() != rotation) apply_rotation((enum EpdRotation)rotation);
}
void ps_rotate(void) {
    if (ps_dual_enabled()) {
        if (!ps_dual_rotate()) ps_toast("请先连接主机再调整方向");
        return;
    }
    // 手动切换继续使用已验收的两种方向；只有自动旋转区分正反。/ Manual switching keeps the accepted pair; automatic rotation distinguishes all four poses.
    apply_rotation(!s_config.landscape ? EPD_ROT_INVERTED_LANDSCAPE : EPD_ROT_INVERTED_PORTRAIT);
}
static int sample_rotation(const sc7a20h_sample_t* sample) {
    if (abs(sample->z_mg) >= 700) return -1;
    // 设备轴+X为默认竖握，-Y为左旋；保留符号才能区分左右转和倒置。
    // Device +X is the default portrait pose and -Y is a left turn; retain signs to distinguish turn direction and inversion.
    if (abs(sample->x_mg) > 700 && abs(sample->y_mg) < 500)
        return sample->x_mg > 0 ? EPD_ROT_INVERTED_PORTRAIT : EPD_ROT_PORTRAIT;
    if (abs(sample->y_mg) > 700 && abs(sample->x_mg) < 500)
        return sample->y_mg < 0 ? EPD_ROT_LANDSCAPE : EPD_ROT_INVERTED_LANDSCAPE;
    return -1;
}
// 方向锁定时不采传感器；双机翻转是显示层180度，候选方向也必须同步翻转，
// 否则翻转端看到的自动旋转会与手持方向相反。
// Locked rotation ignores the sensor. The dual 180-degree flip is display-level, so the
// sensor candidate must be flipped too; otherwise the flipped device turns the wrong way.
static int rotation_flip180(int rotation) {
    // EpdRotation order is landscape, portrait, inverted landscape, inverted portrait;
    // +2 is always the 180-degree counterpart.
    return (rotation + 2) & 3;
}
static bool rotation_consider(int orientation) {
    bool dual = ps_dual_enabled();
    if (dual) {
        if (ps_dual_role() != 1 || ps_dual_rotation_locked() ||
            (orientation != EPD_ROT_PORTRAIT && orientation != EPD_ROT_INVERTED_PORTRAIT))
            orientation = -1;
    } else if (ps_rotation_locked()) {
        orientation = -1;
    }
    bool role_flip = dual && ps_dual_rotation() != ps_dual_base_rotation();
    int candidate = orientation >= 0 && role_flip ? rotation_flip180(orientation) : orientation;
    int current = dual ? ps_dual_rotation() : (int)epd_get_rotation();
    if (candidate < 0 || candidate == current) {
        s_orientation_candidate = -1; s_orientation_matches = 0;
    } else if (candidate != s_orientation_candidate) {
        s_orientation_candidate = candidate; s_orientation_matches = 1;
    } else if (++s_orientation_matches >= 2) {
        if (dual) {
            s_orientation_candidate = -1; s_orientation_matches = 0;
            if (!ps_dual_orient(orientation)) return false;
            ps_dual_rotation_changed();
        } else apply_rotation((enum EpdRotation)candidate);
        return true;
    }
    return false;
}
void ps_book_rotation_enter(app_ctx_t* ctx) {
    if (!ps_dual_enabled()) return;
    epd_set_rotation((enum EpdRotation)ps_dual_rotation());
    s_orientation_candidate = -1; s_orientation_matches = 0;
    if (ctx->sensor_ready) read_pico_sensor_wake(ctx->acc);
}
bool ps_book_rotation_tick(app_ctx_t* ctx, bool idle) {
    if (!ps_dual_enabled()) return false;
    if (!idle || ctx->consumed ||
        (ctx->touch && ctx->touch->touched) || !ps_background_io_allowed() || ps_power_pending()) {
        rotation_consider(-1); return false;
    }
    int before = epd_get_rotation();
    ps_dual_rotation_changed();
    if ((int)epd_get_rotation() != before) return true;
    if (ps_dual_role() != 1 || ps_dual_rotation_locked() || !ctx->sensor_ready || ctx->now_ms - s_acc_ms < 500) return false;
    s_acc_ms = ctx->now_ms;
    read_pico_sensor_wake(ctx->acc);
    sc7a20h_sample_t sample;
    return rotation_consider(read_pico_accel_read(ctx->acc, &sample) == ESP_OK ? sample_rotation(&sample) : -1);
}
void ps_rotation_lock(bool on) {
    s_orientation_candidate = -1; s_orientation_matches = 0;
    s_config.locked = on;
    save_config();
    s_dirty = true;
}
bool ps_rotation_locked(void) {
    return s_config.locked;
}
bool ps_touch_guard_enabled(void) {
    if (ps_dual_enabled()) return ps_dual_guard_enabled();
    return s_touch_guard != 0;
}
bool ps_touch_guard_set(bool enabled) {
    uint8_t next = enabled ? 1 : 0;
    if (ps_dual_enabled()) {
        if (!ps_dual_guard_set(enabled)) return false;
        if (!ps_store("touchguard1", &next, sizeof(next)))
            ps_toast("防误触已同步，但本地保存失败");
        s_touch_guard = next;
        s_dirty = true;
        return true;
    }
    if (next == s_touch_guard) return true;
    if (!ps_store("touchguard1", &next, sizeof(next))) {
        ps_toast("防误触设置保存失败");
        return false;
    }
    s_touch_guard = next;
    s_dirty = true;
    return true;
}
bool ps_demo_home_online(void) {
    return ps_wifi_connected_index() >= 0 && s_config.demo_ha;
}
void ps_demo_connection(bool wifi, bool ha) {
    s_config.demo_wifi = wifi;
    s_config.demo_ha = ha;
    save_config();
    s_dirty = true;
}
int ps_sleep_style(void) {
    return s_config.sleep_style;
}
int ps_excerpt_interval_minutes(void) {
    return s_config.interval;
}
bool ps_apply_sleep_style(int style) {
    if(style<0||style>5)return false;
    uint8_t previous=s_config.sleep_style;s_config.sleep_style=(uint8_t)style;
    if(!ps_store("system_v1",&s_config,sizeof(s_config))){s_config.sleep_style=previous;return false;}
    s_dirty=true;return true;
}
void ps_set_sleep_style(int style) {
    if(!ps_apply_sleep_style(style))ps_toast("锁屏样式保存失败");
}
void ps_set_excerpt_interval(int minutes) {
    const int valid[] = { 5, 15, 30, 60, 360, 720 };
    for (int i = 0; i < 6; i++)
        if (minutes == valid[i]) {
            s_config.interval = minutes;
            save_config();
            s_dirty = true;
        }
}
bool ps_wall_time(struct tm* out) {
    if (!s_clock_valid || !out) return false;
    time_t now = s_epoch + (ps_now() - s_epoch_ms) / 1000 + ps_datetime_timezone_minutes() * 60;
    return gmtime_r(&now, out) != NULL;
}
void ps_clock_set_utc(uint32_t utc) {
    s_clock_valid=true;s_epoch=utc;s_epoch_ms=ps_now();
}
int ps_battery_percent(void) {
    return s_battery;
}
uint8_t ps_status_style(void) {
    return s_status_style < PS_STATUS_STYLE_COUNT ? s_status_style : PS_STATUS_STYLE_LINE;
}
bool ps_status_style_set(uint8_t style) {
    if (style >= PS_STATUS_STYLE_COUNT) return false;
    if (style == s_status_style) return true;
    if (!ps_store("statusstyle1", &style, sizeof(style))) return false;
    s_status_style = style;
    s_dirty = true;
    return true;
}
bool ps_status_percent(void) { return s_status_percent; }
bool ps_status_percent_set(bool enabled) {
    uint8_t value = enabled;
    if (!ps_store("statuspct1", &value, sizeof(value))) return false;
    s_status_percent = enabled; s_dirty = true; return true;
}
uint8_t ps_shelf_card_style(void) {
    return s_shelf_card_style < PS_SHELF_CARD_STYLE_COUNT
        ? s_shelf_card_style : PS_SHELF_CARD_DEFAULT;
}
bool ps_shelf_card_style_set(uint8_t style) {
    if (style >= PS_SHELF_CARD_STYLE_COUNT) return false;
    if (style == s_shelf_card_style) return true;
    if (!ps_store("shelfcards1", &style, sizeof(style))) return false;
    s_shelf_card_style = style;
    s_dirty = true;
    return true;
}
bool ps_charging(void) {
    return s_charging;
}
bool ps_external_power(void) {
    const pmu_snapshot_t *p=read_pico_pmu_get();
    return p&&p->present&&p->status_ok&&p->last_err==ESP_OK&&
        (p->charge_state==PMU_CHARGE_CHARGING||p->charge_state==PMU_CHARGE_FULL_INFERRED);
}
void ps_open_legacy(app_ctx_t* ctx, const app_desc_t* app) {
    if (ctx && app) ctx->request_app = app;
}
void ps_legacy(const app_desc_t* app) {
    extern const app_desc_t app_transfer;
    if (app == &app_transfer) { ps_navigate(PS_TRANSFER); return; }
    if (s_transfer_prepared || s_external_active) { ps_toast("请先结束传书，再打开本机图书"); return; }
    if (!ps_games_prepare_storage()) return;
    s_legacy = app;
    s_dirty = true;
}
static int enabled(int out[QUICK_COUNT]) {
    int n=s_shortcuts.count>QUICK_MAX?QUICK_MAX:s_shortcuts.count;
    for(int i=0;i<n;i++)out[i]=s_shortcuts.ids[i];
    return n;
}
static const char *quick_unavailable(int id) {
    if(local_hold()&&id!=14&&id!=34&&id!=35&&id!=36&&id!=37)return "TF 卡正由电脑管理，请先安全结束传输";
    bool book=s_global_app==&app_book ? app_book_global_is_open() : s_route==PS_READ && ps_reader_is_open();
    if((id==21||id==31)&&s_global_app!=&app_book)return "请先打开一本本机图书";
    if((id==3||id==13||id==15||id==16||id==21||id==31||id==32)&&!book)return "请先打开一本图书";
    if(id==19||id==20) {
        if(s_global_app!=&app_book||!app_book_global_has_selection())return "请先在图书正文中选择文字";
    }
    if((id==27||id==28||id==29) && (s_route!=PS_ARCADEGAME||!ps_games_is_arcade()))return "请先打开2048或贪吃蛇";
    if(id==27 && !ps_games_is_snake())return "仅贪吃蛇支持暂停与继续";
    return NULL;
}
static void open_panel(int panel) {
    ps_games_action(5);s_panel=panel;s_group=0;s_focus=0;s_dirty=true;
}
static void quick_action(int id) {
    const char *reason=quick_unavailable(id);
    s_panel=s_fan=s_fan_candidate=0;s_tracking=false;
    if(reason){ps_toast(reason);return;}
    switch(id) {
    case 0:ps_switch(0);break;
    case 1:
        if(s_global_app==&app_book){s_global_redraw=app_book_global_action(s_ctx,BOOK_GLOBAL_READING);break;}
        if(!resume_real_book()){ps_switch(0);ps_reader_open(-1);}break;
    case 2:case 17:case 18:case 38:case 39:
        if(s_global_app==&app_book){s_global_redraw=app_book_global_action(s_ctx,(id==17||id==38)?BOOK_GLOBAL_HIGHLIGHTS:(id==18||id==39)?BOOK_GLOBAL_NOTES:BOOK_GLOBAL_RECORDS);break;}
        ps_switch(0);ps_navigate(PS_RECORDS);if(id==17||id==18||id==38||id==39)ps_reader_records_tab(id==17||id==38?1:0);break;
    case 3:
        if(s_global_app==&app_book) {
            s_global_redraw=app_book_global_action(s_ctx,BOOK_GLOBAL_FONT);
        } else {
            ps_navigate(PS_READING_SETTINGS);
        }
        break;
    case 4:case 5:ps_switch(1);ps_home_action(id==4?0:1);break;
    case 6:ps_navigate(PS_HA_SETTINGS);ps_home_action(2);break;
    case 7:ps_sleep();break;
    case 8:ps_navigate(PS_WIFI);break;
    case 9:
        if (ps_dual_enabled()) {
            bool on = ps_dual_rotation_locked();
            if (ps_dual_lock_set(!on)) ps_toast(on ? "已解除方向锁定" : "已锁定当前方向");
        } else {
            ps_rotation_lock(!s_config.locked);
            ps_toast(s_config.locked ? "已锁定当前方向" : "已解除方向锁定");
        }
        break;
    case 10:s_quick_scroll=0;s_quick_mode=0;ps_navigate(PS_QUICK_SETTINGS);break;
    case 11:ps_switch(2);break;
    case 12:ps_navigate(PS_PRINTERS);break;
    case 13:
        if(s_global_app==&app_book)s_global_redraw=app_book_global_action(s_ctx,BOOK_GLOBAL_TOOLS);
        else ps_reader_tools();
        break;
    case 14:ps_global_open_settings(s_ctx);break;
    case 15:
        if(s_global_app==&app_book)s_global_redraw=app_book_global_action(s_ctx,BOOK_GLOBAL_TOC);
        else ps_reader_toc();
        break;
    case 16:
        if(s_global_app==&app_book)s_global_redraw=app_book_global_action(s_ctx,BOOK_GLOBAL_BOOKMARK);
        else ps_reader_action(3);
        break;
    case 19:case 20:case 21:
        s_global_redraw=app_book_global_action(s_ctx,id==19?BOOK_GLOBAL_HIGHLIGHT:id==20?BOOK_GLOBAL_NOTE:BOOK_GLOBAL_FAVORITE);break;
    case 22:ps_navigate(PS_TRANSFER);break;
    case 23:ps_navigate(PS_SLEEP_SETTINGS);break;
    case 24:ps_navigate(PS_HA_SETTINGS);break;
    case 25:case 26:ps_games_action(id==25?0:1);break;
    case 27:ps_games_action(2);break;
    case 28:ps_games_action(3);break;
    case 29:ps_games_action(4);break;
    case 30:ps_navigate(PS_INPUT_SETTINGS);break;
    case 31:
        if(s_global_app==&app_book)s_global_redraw=app_book_global_action(s_ctx,BOOK_GLOBAL_PAGE);
        else ps_toast("请先打开一本本机图书");
        break;
    case 32:
        if(s_global_app==&app_book)s_global_redraw=app_book_global_action(s_ctx,BOOK_GLOBAL_BOOKMARKS);
        else {ps_navigate(PS_RECORDS);ps_reader_records_tab(2);}break;
    case 33:ps_navigate(PS_STORAGE);break;
    case 34:ps_navigate(PS_USB_TRANSFER);break;
    case 35:if(s_global_app==&app_book)s_global_redraw=APP_REDRAW_FULL;else s_force_full=true;break;
    case 36: {
        char text[80];if(s_battery<0)snprintf(text,sizeof(text),"电量读取中");
        else snprintf(text,sizeof(text),"当前电量 %d%% · %s",s_battery,s_charging?"正在充电":"使用电池");
        ps_toast(text);break;
    }
    case 37:ps_toast(ps_home_connection_label());break;
    case 40: {
        bool on = ps_touch_guard_enabled();
        if (ps_touch_guard_set(!on)) ps_toast(on ? "已关闭贴合防误触" : "已开启贴合防误触");
        break;
    }
    default:break;
    }
    s_dirty=true;
}
typedef struct {
    int id, x, y, r, ring, group;
} fan_item_t;
static bool fan_editor_active(void) {
    return !s_fan&&s_route==PS_QUICK_SETTINGS&&s_quick_edit.active&&s_quick_mode==0&&!s_panel;
}
static bool fan_frame_active(void) {return s_fan||fan_editor_active();}
static int fan_frame_selected(void) {
    return s_fan?s_fan_selected:s_quick_edit.dragging?s_quick_edit.target:s_quick_edit.selected;
}
static int fan_frame_side(void) {return fan_editor_active()?s_quick_edit.side:s_fan_side;}
static float fan_scale(void) {
    ps_view_t v=ps_view();return fminf(v.w,v.h)/348.f;
}
static int fan_layout_slots(fan_item_t out[QUICK_COUNT],const uint8_t slots[FAN_SLOTS],int side) {
    ps_view_t v=ps_view();float scale=fan_scale();
    static const int counts[3]={3,5,6};static const float radii[3]={152.f,225.f,298.f};
    // 圆心贴合屏幕底角，三层等距；空位保留几何，只在编排时显示与命中。
    // Exact screen-corner origin and equal ring spacing; empty positions stay fixed and are editable only.
    float edge=(44.f*scale+360.f*10.f/58.5f)/2.f+3.f*scale;
    int at=0;for(int ring=0;ring<3;ring++) {
        float radius=radii[ring]*scale,guard=asinf(fminf(.99f,edge/radius));
        for(int j=0;j<counts[ring];j++) {
            float angle=guard+j*(3.14159265f/2.f-2.f*guard)/(counts[ring]-1);
            int id=slots[at]<QUICK_COUNT?slots[at]:-1;
            out[at++]=(fan_item_t){id,(int)lroundf((side?v.w:0)+(side?-1:1)*radius*cosf(angle)),
                (int)lroundf(v.h-radius*sinf(angle)),(int)lroundf(22.f*scale),ring,-1};
        }
    }return at;
}
static int fan_layout(fan_item_t out[QUICK_COUNT]) {
    return fan_layout_slots(out,fan_editor_active()?s_quick_edit.slots:s_quick_slots,fan_frame_side());
}
static int fan_pick(const fan_item_t* items,int n,int x,int y,int previous) {
    ps_view_t v=ps_view();if(!n||x<0||x>=v.w||y<24||y>=v.h)return -1;
    float scale=fan_scale(),dx=fan_frame_side()?v.w-x:x,dy=v.h-y,radius=hypotf(dx,dy);
    if(radius<108.f*scale)return -1;
    // 径向层互斥，扩大光圈仅绘制，不参与任何触摸判定。
    // Disjoint radial bands use compact touch targets; the large visual halo never participates.
    int ring=radius<(152.f+225.f)*scale/2.f?0:radius<(225.f+298.f)*scale/2.f?1:2;
    int best=-1;float distance=1e9f,prior=1e9f;bool edit=fan_editor_active();
    for(int i=0;i<n;i++)if(items[i].ring==ring&&(edit||items[i].id>=0)) {
        float d=hypotf(items[i].x-x,items[i].y-y);
        if(i==previous)prior=d;
        if(d<distance){distance=d;best=i;}
    }
    if(previous>=0&&previous<n&&(edit||items[previous].id>=0)&&items[previous].ring==ring&&prior<35.f*scale&&prior<distance+8.f*scale)return previous;
    return distance<35.f*scale?best:-1;
}
static void fan_move(int x,int y) {
    fan_item_t a[QUICK_COUNT];int n=fan_layout(a),best=fan_pick(a,n,x,y,s_fan_selected);
    bool changed=best!=s_fan_selected;s_fan_selected=best;
    if(changed){s_dirty=true;s_fast=true;}
}
static void fan_release(int x,int y) {
    // 松手以最终坐标重新判定，不能执行上一次MOVE留下的动作。
    // Re-pick the release coordinate instead of executing a stale MOVE.
    fan_move(x,y);fan_item_t a[QUICK_COUNT];int n=fan_layout(a),chosen=s_fan_selected;
    s_fan_candidate=s_fan=false;s_fan_selected=-1;s_fast=true;fan_present_reset();
    if(chosen>=0&&chosen<n&&a[chosen].id>=0)quick_action(a[chosen].id);
    s_dirty=true;
}
static int fan_prompt_x(void) {
    ps_view_t v=ps_view();return v.landscape?(fan_frame_side()?16:340):16;
}
// 快捷动作的运行态：开机、方向锁定、贴合防误触等开关在圆面内部换色。
// Runtime quick-action states: toggles such as Wi-Fi, rotation lock and touch guard recolor the disc.
static void render_fan_base(uint8_t* fb) {
    ps_view_t v=ps_view();ps_fill(fb,0,0,v.w,v.h,15);bool edit=fan_editor_active();
    fan_item_t a[QUICK_COUNT];int n=fan_layout(a);float scale=fan_scale();int ix=fan_prompt_x();char detail[100];
    ps_text_fixed(fb,ix,80,12,edit?"编排快捷菜单":"快捷操作",5);
    if(edit) {
        ps_text_fixed(fb,ix,32,18,"长按拖动交换位置",2);
        ps_text_fixed(fb,ix,57,11,"轻点替换；完成后才会保存",5);
        ps_text_fixed(fb,ix,145,11,s_quick_edit.save_failed?"保存失败，请重试；草稿仍保留":"左右共用同一布局 · 所有位置均可更换",5);
        int bw=v.landscape?80:76,gap=8;
        ps_button(fb,ix,178,bw,34,"取消",false);ps_button(fb,ix+bw+gap,178,bw,34,"撤销",false);ps_button(fb,ix+2*(bw+gap),178,bw,34,"完成",true);
        ps_button(fb,ix,220,bw,32,"恢复默认",false);ps_button(fb,ix+bw+gap,220,bw,32,"左右镜像",false);
        ps_button(fb,ix+2*(bw+gap),220,bw,32,"移除",false);
    } else {
        snprintf(detail,sizeof(detail),"已放置 %d / 14 · 三层直达功能",quick_placed(s_quick_slots));
        ps_text_fixed(fb,ix,145,12,detail,5);ps_button(fb,ix,32,76,34,"编排",false);ps_icon(fb,v.w-36,32,22,PI_CLOSE,2);
    }
    for(int i=0;i<n;i++) {
        if(a[i].id<0&&!edit)continue;
        int r=a[i].r;
        ps_box(fb,a[i].x-r,a[i].y-r,r*2,r*2,r,15,true);
        ps_box(fb,a[i].x-r,a[i].y-r,r*2,r*2,r,a[i].id<0?6:0,false);
        ps_icon_t icon=a[i].id<0?PI_PLUS:s_quick[a[i].id].icon;
        int sz=(int)lroundf(21.f*scale);bool unavailable=!edit&&a[i].id>=0&&quick_unavailable(a[i].id);
        ps_icon(fb,a[i].x-sz/2,a[i].y-r+(int)lroundf(3.f*scale),sz,icon,a[i].id<0||unavailable?6:2);
        // 两字短名是静态按钮的一部分；滑动只更新圈外光圈和固定标题。
        // Short names belong to the fixed base; motion changes only the outside halo and fixed title.
        ps_text_fixed(fb,a[i].x-15,a[i].y+4,11,a[i].id<0?"添加":s_quick_short[a[i].id],a[i].id<0||unavailable?6:2);
    }
    int side=fan_frame_side();ps_center(fb,side?v.w-62:62,v.h-42,10,"安全区",5);
    ps_center(fb,side?v.w-62:62,v.h-25,10,edit?"拖回取消":"松手取消",5);
}

static void render_fan_cursor(uint8_t *fb) {
    fan_item_t a[QUICK_COUNT];int n=fan_layout(a),selected=fan_frame_selected();bool edit=fan_editor_active();
    if(selected>=0&&selected<n&&(edit||a[selected].id>=0)) {
        EpdRect buttons[QUICK_COUNT];int count=0;
        for(int i=0;i<n;i++)if(edit||a[i].id>=0)buttons[count++]=(EpdRect){a[i].x-a[i].r,a[i].y-a[i].r,a[i].r*2,a[i].r*2};
        int halo=(int)lroundf(22.f*fan_scale()+360.f*5.f/58.5f);
        ps_cursor_ring(fb,a[selected].x,a[selected].y,halo,buttons,count);
    }
    const char *label=selected>=0&&selected<n?(a[selected].id<0?"空位置":s_quick[a[selected].id].label):edit?"轻点选择功能位置":"滑向图标，松手打开";
    ps_text_fixed(fb,fan_prompt_x(),106,25,label,2);
}
static void render_fan(uint8_t *fb) {render_fan_base(fb);render_fan_cursor(fb);}
static EpdRect fan_clip_area(EpdRect r) {
    ps_view_t v=ps_view();int x0=r.x<0?0:r.x,y0=r.y<0?0:r.y;
    int x1=r.x+r.width>v.w?v.w:r.x+r.width,y1=r.y+r.height>v.h?v.h:r.y+r.height;
    return x1>x0&&y1>y0?(EpdRect){x0,y0,x1-x0,y1-y0}:(EpdRect){0};
}
static void fan_add_area(EpdRect areas[3],int *count,EpdRect next) {
    next=fan_clip_area(next);if(!next.width||!next.height)return;
    for(int i=0;i<*count;) {
        EpdRect a=areas[i];
        if(a.x<=next.x+next.width&&next.x<=a.x+a.width&&a.y<=next.y+next.height&&next.y<=a.y+a.height) {
            int x=a.x<next.x?a.x:next.x,y=a.y<next.y?a.y:next.y;
            int right=a.x+a.width>next.x+next.width?a.x+a.width:next.x+next.width;
            int bottom=a.y+a.height>next.y+next.height?a.y+a.height:next.y+next.height;
            next=(EpdRect){x,y,right-x,bottom-y};areas[i]=areas[--*count];i=0;
        }else i++;
    }
    areas[(*count)++]=next;
}
static EpdRect fan_cursor_area(int selected) {
    fan_item_t a[QUICK_COUNT];int n=fan_layout(a);
    if(selected<0||selected>=n)return (EpdRect){0};
    int r=(int)lroundf(22.f*fan_scale()+360.f*5.f/58.5f)+3;
    return (EpdRect){a[selected].x-r,a[selected].y-r,r*2+1,r*2+1};
}
static void fan_area_pixels(uint8_t *fb,const uint8_t *source,EpdRect area,bool threshold) {
    EpdRect r=ui_rotate_rect_to_fb(ps_rect(area.x,area.y,area.width,area.height));
    int width=epd_width(),height=epd_height();
    for(int y=r.y;y<r.y+r.height&&y<height;y++)for(int x=r.x;x<r.x+r.width&&x<width;x++) {
        if(x<0||y<0)continue;
        size_t at=(size_t)y*width/2+x/2;int shift=(x&1)*4;
        unsigned value=((source?source[at]:fb[at])>>shift)&15u;
        if(threshold)value=value>=8?15:0;
        fb[at]=(fb[at]&~(15u<<shift))|(value<<shift);
    }
}
// 先缩放再合并，覆盖独立舍入的区域边缘；每次交互只扫一轮快速波形。
// Scale before union to contain separately rounded edges; scan one fast waveform per interaction.
static EpdRect fast_damage_pixels(const EpdRect *areas,int count) {
    EpdRect damage=ps_rect(areas[0].x,areas[0].y,areas[0].width,areas[0].height);
    for(int i=1;i<count;i++){
        EpdRect r=ps_rect(areas[i].x,areas[i].y,areas[i].width,areas[i].height);
        int x=damage.x<r.x?damage.x:r.x,y=damage.y<r.y?damage.y:r.y;
        int right=damage.x+damage.width>r.x+r.width?damage.x+damage.width:r.x+r.width;
        int bottom=damage.y+damage.height>r.y+r.height?damage.y+damage.height:r.y+r.height;
        damage=(EpdRect){x,y,right-x,bottom-y};
    }
    return damage;
}
static uint64_t fan_paint_key(void) {
    uint64_t hash=UINT64_C(14695981039346656037);fan_item_t a[QUICK_COUNT];int n=fan_layout(a);
    int state[]={fan_frame_side(),fan_editor_active(),s_quick_edit.save_failed,ps_route(),ps_view().w,ps_view().h};
    for(size_t i=0;i<sizeof(state)/sizeof(state[0]);i++)hash=(hash^(unsigned)state[i])*UINT64_C(1099511628211);
    for(int i=0;i<n;i++) {
        int fields[]={a[i].id,a[i].x,a[i].y,a[i].r,a[i].ring,a[i].group};
        for(size_t j=0;j<sizeof(fields)/sizeof(fields[0]);j++)hash=(hash^(unsigned)fields[j])*UINT64_C(1099511628211);
    }
    return hash;
}
static bool fan_present(app_ctx_t *ctx) {
    uint64_t key=fan_paint_key();int rotation=epd_get_rotation();
    bool same=s_fan_paint.valid&&s_fan_paint.owner==ctx->hl&&s_fan_paint.key==key&&s_fan_paint.rotation==rotation;
    s_fast=false;
    int selected=fan_frame_selected();
    if(same&&s_fan_paint.selected==selected)return true;
    EpdRect areas[3];int count=0;bool partial=same&&s_fan_paint.base;
    if(partial) {
        fan_add_area(areas,&count,fan_cursor_area(s_fan_paint.selected));
        fan_add_area(areas,&count,fan_cursor_area(selected));
        ps_view_t v=ps_view();int ix=fan_prompt_x();
        fan_add_area(areas,&count,(EpdRect){ix-2,100,v.w-ix,42});
        for(int i=0;i<count;i++)fan_area_pixels(ctx->fb,s_fan_paint.base,areas[i],false);
    }else {
        // 基底只在打开或明确改变布局时生成；暂不把后台提示覆盖到滑动菜单上。
        // Build the base only on entry/layout changes; defer unrelated transient toasts until the fan closes.
        render_fan_base(ctx->fb);ps_corner_controls(ctx->fb);
        if(!s_fan_paint.allocation_attempted){
            s_fan_paint.allocation_attempted=true;
            s_fan_paint.base=heap_caps_malloc((size_t)epd_width()*epd_height()/2,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
        }
        if(s_fan_paint.base)memcpy(s_fan_paint.base,ctx->fb,(size_t)epd_width()*epd_height()/2);
    }
    render_fan_cursor(ctx->fb);
    enum EpdDrawError error=EPD_DRAW_SUCCESS;
    if(partial) {
        // 非 duo 主线做法：先合并新旧光圈和提示区，再做一次局部 GL16。
        // 分开做 DU 会留下未覆盖的光圈边缘，也会让滑动过程断续闪烁。
        // Non-duo behavior: union old/new halos and prompt, then one local GL16.
        // 合并后的单区域只用一次 DU，避免 GL16 在滑动过程中反复闪屏。
        // One unioned DU scan per move; GL16 caused repeated flashing while sliding.
        error=update_display_area_with(ctx->hl,&E0470_WAVEFORM,MODE_DU,fast_damage_pixels(areas,count));
    }else error=update_display_with(ctx->hl,&E0470_WAVEFORM,MODE_GL16);
    int fallback=-1;if(error!=EPD_DRAW_SUCCESS)fallback=update_display_full(ctx->hl);
    s_fan_paint.owner=ctx->hl;s_fan_paint.key=key;s_fan_paint.rotation=rotation;s_fan_paint.selected=selected;
    s_fan_paint.valid=error==EPD_DRAW_SUCCESS||fallback==EPD_DRAW_SUCCESS;
    ESP_LOGI("pico_system","fan present partial=%d areas=%d selected=%d epd_err=%d fallback_err=%d",partial,count,selected,(int)error,fallback);
    return true;
}
static int group_items(int out[QUICK_COUNT]) { return enabled(out); }
static EpdRect panel_group_rect(int i) {
    ps_view_t v=ps_view();int width=v.landscape?440:v.w-24,x=(v.w-width)/2;
    int count=ps_navigation_count(),cols=v.landscape?2:1,cell=(width-24-(cols-1)*12)/cols;
    int row=i/cols,col=i%cols,h=v.landscape?60:56;
    (void)count;return (EpdRect){x+12+col*(cell+12),88+row*(h+8),cell,h};
}
// 同一对矩形用于横竖屏绘制与命中，不增加快捷卡片或持久化位。/ Share portrait/landscape drawing and hits without adding a card or persistence bit.
static EpdRect panel_reading_rect(int i) {
    ps_view_t v=ps_view();int cols=v.landscape?3:2,cw=(v.w-48-(cols-1)*10)/cols;
    return (EpdRect){24+(i%cols)*(cw+10),88+(i/cols)*76,cw,68};
}
static void render_panel(uint8_t *fb) {
    ps_view_t v=ps_view();ps_fill(fb,0,24,v.w,v.h-24,15);
    ps_heading(fb,s_panel==3?"切换页面":"快捷操作",s_panel==3?"双击中键切换 · 单击返回":"与滑动扇形共享启用项目和顺序");
    if(s_panel==3) {
        for(int i=0;i<ps_navigation_count();i++){const ps_navigation_entry_t *item=ps_navigation_item(i);EpdRect r=panel_group_rect(i);ps_button(fb,r.x,r.y+38,r.width,r.height,item->label,s_focus==i);ps_icon(fb,r.x+12,r.y+53,22,item->icon,s_focus==i?15:2);}return;
    }
    int ids[QUICK_COUNT],n=group_items(ids),cols=v.landscape?3:2,rows=(v.h-220)/76,per=cols*rows,start=s_group*per;
    for(int k=0;k<per&&start+k<n;k++) {
        int id=ids[start+k];EpdRect r=panel_reading_rect(k);r.y+=38;
        const char *why=quick_unavailable(id);int gray=why?9:2;
        ps_box(fb,r.x,r.y,r.width,r.height,5,11,false);ps_icon(fb,r.x+10,r.y+12,22,s_quick[id].icon,gray);
        ps_text(fb,r.x+40,r.y+12,12,s_quick[id].label,gray);
        ps_text(fb,r.x+10,r.y+41,10,why?"当前不可用":"点击打开",gray);
    }
    if(!n)ps_text(fb,24,145,14,"尚未添加快捷入口",5);
    if(n>per){ps_button(fb,24,v.h-76,88,32,"上一页",false);ps_button(fb,v.w-112,v.h-76,88,32,"下一页",false);}
    ps_icon(fb,v.w/2-11,v.h-72,22,PI_SETTINGS,2);
}

enum { DEVICE_GROUP_COUNT=6, DEVICE_ABOUT=6, DEVICE_SETTING_COUNT=20 };
enum { DEVICE_ACTION_ROUTE=0, DEVICE_ACTION_TEST_AWAKE, DEVICE_ACTION_STATUS_STYLE,
       DEVICE_ACTION_SHELF_CARD };
typedef struct {const char *label,*detail;uint8_t icon,first,count;} device_group_t;
typedef struct {const char *label,*detail;uint8_t icon,route,action;} device_setting_t;
static const device_group_t device_groups[] = {
    {"存储与传输","TF 卡、图书与文件",PI_SD,0,1},
    {"网络与连接","Wi-Fi、手机与蓝牙",PI_LINK,1,3},
    {"阅读与输入","排版与笔记输入",PI_BOOK,4,3},
    {"外观与操作","图标、导航、菜单与动画",PI_LAYOUT,7,6},
    {"应用与服务","应用、家庭与打印机",PI_GRID,13,4},
    {"系统与电源","锁屏、时间与测试",PI_POWER,17,3}
};
static const device_setting_t device_settings[] = {
    {NULL,NULL,PI_SD,PS_TRANSFER,DEVICE_ACTION_ROUTE},
    {"Wi-Fi","网络连接",PI_WIFI,PS_WIFI,DEVICE_ACTION_ROUTE},
    {"手机与电脑管理","扫码与电脑连接",PI_LINK,PS_WIRELESS_MANAGE,DEVICE_ACTION_ROUTE},
    {"蓝牙翻页器","外接翻页器",PI_BLUETOOTH,PS_BLUETOOTH,DEVICE_ACTION_ROUTE},
    {"阅读排版","字体与行距",PI_SLIDERS,PS_READING_SETTINGS,DEVICE_ACTION_ROUTE},
    {"笔记输入","语言与键盘",PI_NOTE,PS_INPUT_SETTINGS,DEVICE_ACTION_ROUTE},
    {"双机阅读","主从配对与书库同步",PI_LINK,PS_DUAL,DEVICE_ACTION_ROUTE},
    {"首页导航","排序与隐藏",PI_GRID,PS_NAV_SETTINGS,DEVICE_ACTION_ROUTE},
    {"快捷菜单","功能位置与排序",PI_GRID,PS_QUICK_SETTINGS,DEVICE_ACTION_ROUTE},
    {"提示音","触摸与充电提示",PI_SOUND,PS_SOUND,DEVICE_ACTION_ROUTE},
    {"翻页动画","开关与灰阶",PI_PLAY,PS_ANIMATION_SETTINGS,DEVICE_ACTION_ROUTE},
    {"状态栏图标","墨线极简",PI_BATTERY,PS_DEVICE,DEVICE_ACTION_STATUS_STYLE},
    {"上次阅读卡片","默认",PI_BOOK,PS_DEVICE,DEVICE_ACTION_SHELF_CARD},
    {"应用集","下载、排序与管理",PI_GRID,PS_APPS,DEVICE_ACTION_ROUTE},
    {"Home Assistant","连接与实体",PI_HOME,PS_HA_SETTINGS,DEVICE_ACTION_ROUTE},
    {"3D 打印机","Bambu Lab 与 Formlabs",PI_PRINTER,PS_PRINTER_SETTINGS,DEVICE_ACTION_ROUTE},
    {"棋类小游戏","棋局与对手",PI_GAME,PS_GAMES,DEVICE_ACTION_ROUTE},
    {"电源与锁屏","锁屏、重启与关机",PI_POWER,PS_POWER,DEVICE_ACTION_ROUTE},
    {"日期与时间","手动与网络授时",PI_CLOCK,PS_DATETIME,DEVICE_ACTION_ROUTE},
    {"保持唤醒","测试时不自动休眠",PI_PAUSE,PS_DEVICE,DEVICE_ACTION_TEST_AWAKE}
};
// 根分组与二级项目共用几何，绘制和触摸始终使用相同边界。
// Root groups and child items share geometry, keeping paint and touch boundaries identical.
static EpdRect device_row_rect(int index,bool root){
    ps_view_t v=ps_view();int rows=v.landscape?3:6;
    int rh=root?(v.landscape?48:60):(v.landscape?60:76);
    int top=root?(v.landscape?146:158):(v.landscape?112:126);
    int cols=v.landscape?2:1,cw=(v.w-32-(cols-1)*12)/cols;
    return (EpdRect){16+(index/rows)*(cw+12),top+(index%rows)*rh,cw,rh};
}
static PICO_STORE_COLD void device_row_render(uint8_t *fb,EpdRect r,const char *label,const char *detail,ps_icon_t icon,int toggle){
    ps_surface(fb,r.x,r.y,r.width,r.height-4,false);
    ps_icon(fb,r.x+12,r.y+(r.height-26)/2,22,icon,2);
    ps_text(fb,r.x+46,r.y+8,14,label,1);
    ps_text(fb,r.x+46,r.y+30,10,detail,5);
    int cy=r.y+(r.height-4)/2;
    if(toggle)ps_switch_draw(fb,r.x+r.width-60,cy-13,48,26,app_settings_test_awake(),true);
    else{ps_line(fb,r.x+r.width-21,cy-4,r.x+r.width-17,cy,6);ps_line(fb,r.x+r.width-17,cy,r.x+r.width-21,cy+4,6);}
}
static const char *status_style_name(uint8_t style){
    if(style==PS_STATUS_STYLE_BOOK)return "书页索引";
    return "墨线极简";
}
static const char *shelf_card_style_name(uint8_t style){
    if(style==PS_SHELF_CARD_BOARDING)return "登机牌";
    if(style==PS_SHELF_CARD_TRAIN)return "火车票";
    if(style==PS_SHELF_CARD_CINEMA)return "电影院票根";
    return "默认";
}
static void status_wifi_draw(uint8_t *fb,int x,int y,bool book){
    bool usb=ps_usb_background();
    bool online=usb||read_pico_connectivity_online();
    if(usb)ps_icon(fb,x,y,16,PI_USB,3);
    else {
        ps_wifi_signal(fb,x,y,16,online?s_wifi_level:3,online?3:8);
        if(!online)ps_line(fb,x+1,y+15,x+15,y+1,3);
    }
    (void)book;
}
static void status_pair_draw(uint8_t *fb,int x,int y,bool book,bool linked,int role){
    if(!linked)return;
    if(book){
        const int size=12,gap=8;
        int left=x,right=left+size+gap;
        ps_icon(fb,left,y,size,PI_BOOK,role==1?1:7);
        ps_icon(fb,right,y,size,PI_BOOK,role==2?1:7);
        if(linked){
            ps_fill(fb,left+size+1,y+size/2,2,1,6);
            ps_fill(fb,right-3,y+size/2,2,1,6);
        }
        if(role==1)ps_line(fb,left+2,y+size+1,left+size-2,y+size+1,3);
        else if(role==2)ps_line(fb,right+2,y+size+1,right+size-2,y+size+1,3);
    }else{
        const int width=8,height=12,gap=8;
        int left=x,right=left+width+gap;
        ps_box(fb,left,y,width,height,2,role==1?1:7,false);
        ps_box(fb,right,y,width,height,2,role==2?1:7,false);
        if(linked){
            ps_fill(fb,left+width+1,y+height/2,2,1,6);
            ps_fill(fb,right-3,y+height/2,2,1,6);
        }
        if(linked&&role==1)ps_fill(fb,left+width-3,y+height-3,3,2,2);
        else if(linked&&role==2)ps_fill(fb,right+width-3,y+height-3,3,2,2);
    }
}
static void status_battery_draw(uint8_t *fb,int x,int y,bool book){
    const int width=19,height=10,radius=4;
    ps_box(fb,x,y,width,height,radius,3,false);
    ps_box(fb,x+width+1,y+3,2,4,1,4,true);
    int percent=s_battery;
    if(percent<0){
        ps_fill(fb,x+3,y+4,3,2,7);
        ps_fill(fb,x+8,y+4,3,2,7);
        ps_fill(fb,x+13,y+4,3,2,7);
    }else if(percent>100)percent=100;
    if(percent>=0){
        int inner=width-4;
        if(book){
            int segment=(inner-3)/4;
            for(int i=0;i<4;i++){
                int threshold=i*25+1;
                if(percent>=threshold)ps_box(fb,x+2+i*(segment+1),y+2,segment,height-4,1,3,true);
            }
        }else{
            int fill=inner*percent/100;
            if(fill>0)ps_box(fb,x+2,y+2,fill,height-4,2,3,true);
        }
    }
    if(s_charging){
        ps_line(fb,x+10,y+1,x+7,y+5,15);
        ps_line(fb,x+7,y+5,x+10,y+5,15);
        ps_line(fb,x+10,y+5,x+8,y+9,15);
    }
}
// 实际状态栏与选择预览共用同一相对坐标。/ One relative layout for the live strip and its picker.
static void status_strip_draw(uint8_t *fb,int left,int right,int top,uint8_t style,bool linked,int role){
    bool book=style==PS_STATUS_STYLE_BOOK;
    status_wifi_draw(fb,left,top+3,book);
    status_pair_draw(fb,left+22,top+4,book,linked,role);
    bool percent=ps_status_percent();
    if(percent){
        char level[12];
        if(s_battery>=0)snprintf(level,sizeof(level),"%d%%",s_battery>100?100:s_battery);
        else snprintf(level,sizeof(level),"--%%");
        int text_x=right-ps_text_width(9,level);
        status_battery_draw(fb,text_x-5-22,top+6,book);
        ps_text_fixed(fb,text_x,top+4,9,level,5);
    }else status_battery_draw(fb,right-22,top+6,book);
}
static void status_icons_draw(uint8_t *fb,ps_view_t v,bool book,bool linked){
    (void)book;status_strip_draw(fb,12,v.w-12,0,ps_status_style(),linked,ps_dual_role());
}
typedef struct {EpdRect panel,option[PS_STATUS_STYLE_COUNT],percent,cancel;} status_style_picker_t;
static status_style_picker_t status_style_picker_layout(void){
    ps_view_t v=ps_view();status_style_picker_t r={0};
    int h=v.landscape?312:390,w=v.landscape?520:v.w-32;
    r.panel=(EpdRect){(v.w-w)/2,(v.h-h)/2,w,h};
    int rh=v.landscape?58:84,step=v.landscape?64:92,top=v.landscape?62:72;
    for(int i=0;i<PS_STATUS_STYLE_COUNT;i++)r.option[i]=(EpdRect){r.panel.x+16,r.panel.y+top+i*step,w-32,rh};
    r.percent=(EpdRect){r.panel.x+16,r.panel.y+top+2*step+8,w-32,44};
    r.cancel=(EpdRect){r.panel.x+w-112,r.panel.y+h-56,96,40};
    return r;
}
static void status_style_picker_render(uint8_t *fb){
    if(!s_status_style_picker)return;
    ps_view_t v=ps_view();status_style_picker_t r=status_style_picker_layout();
    ps_modal_background(fb);ps_surface(fb,r.panel.x,r.panel.y,r.panel.width,r.panel.height,false);
    ps_text(fb,r.panel.x+16,r.panel.y+14,16,"状态栏图标",1);
    ps_text(fb,r.panel.x+16,r.panel.y+40,11,"选择网络、双机与电量的外观",5);
    for(int i=0;i<PS_STATUS_STYLE_COUNT;i++){
        EpdRect row=r.option[i];bool selected=i==ps_status_style();
        ps_box(fb,row.x,row.y,row.width,row.height,9,selected?2:12,true);
        ps_box(fb,row.x+1,row.y+1,row.width-2,row.height-2,8,15,true);
        if(selected)ps_icon(fb,row.x+10,row.y+12,14,PI_CHECK,1);
        ps_text_fixed(fb,row.x+32,row.y+10,14,status_style_name((uint8_t)i),1);
        if(v.landscape){
            ps_text_fixed(fb,row.x+32,row.y+34,10,selected?"当前使用":"轻点使用",5);
            status_strip_draw(fb,row.x+row.width/2,row.x+row.width-16,row.y+12,(uint8_t)i,true,1);
        }else{
            status_strip_draw(fb,row.x+32,row.x+row.width-16,row.y+34,(uint8_t)i,true,1);
            ps_text_fixed(fb,row.x+32,row.y+61,10,selected?"当前使用":"轻点使用",5);
        }
    }
    ps_toggle(fb,r.percent.x,r.percent.y,r.percent.width,r.percent.height,"电量百分比",ps_status_percent(),true);
    ps_button(fb,r.cancel.x,r.cancel.y,r.cancel.width,r.cancel.height,"取消",false);
}
typedef struct {EpdRect panel,option[PS_SHELF_CARD_STYLE_COUNT],cancel;} shelf_card_picker_t;
static shelf_card_picker_t shelf_card_picker_layout(void){
    ps_view_t v=ps_view();shelf_card_picker_t r={0};
    if(v.landscape){
        r.panel=(EpdRect){(v.w-560)/2,20,560,320};
        int width=(r.panel.width-52)/2;
        for(int i=0;i<PS_SHELF_CARD_STYLE_COUNT;i++){
            r.option[i]=(EpdRect){r.panel.x+20+(i%2)*(width+12),
                                  r.panel.y+60+(i/2)*100,width,90};
        }
        r.cancel=(EpdRect){r.panel.x+r.panel.width-112,r.panel.y+r.panel.height-56,96,40};
    }else{
        r.panel=(EpdRect){16,76,v.w-32,490};
        for(int i=0;i<PS_SHELF_CARD_STYLE_COUNT;i++)
            r.option[i]=(EpdRect){r.panel.x+14,r.panel.y+64+i*88,r.panel.width-28,78};
        r.cancel=(EpdRect){r.panel.x+r.panel.width-112,r.panel.y+r.panel.height-56,96,40};
    }
    return r;
}
static void shelf_card_motif(uint8_t *fb,int x,int y,int w,int h,uint8_t style){
    ps_box(fb,x,y,w,h,5,1,true);ps_box(fb,x+1,y+1,w-2,h-2,4,15,true);
    int split=x+w-18;
    if(style==PS_SHELF_CARD_TRAIN){
        int line=y+h-12;
        ps_fill(fb,x+2,line+2,w-4,8,13);
        for(int xx=x+5;xx<x+w-4;xx+=6)ps_line(fb,xx,line,xx+2,line,7);
        ps_box(fb,x-3,line-3,6,6,3,15,true);ps_box(fb,x+w-3,line-3,6,6,3,15,true);
    }else{
        ps_fill(fb,split+2,y+3,14,h-6,13);
        for(int yy=y+6;yy<y+h-4;yy+=6)ps_line(fb,split,yy,split,yy+2,7);
        ps_box(fb,split-4,y-4,8,8,4,15,true);ps_box(fb,split-4,y+h-4,8,8,4,15,true);
        if(style==PS_SHELF_CARD_DEFAULT){
            ps_fill(fb,x+2,y+4,5,h-8,13);ps_line(fb,x+8,y+6,x+8,y+h-6,7);
        }
        if(style==PS_SHELF_CARD_CINEMA){
            for(int k=0;k<7;k++){ps_fill(fb,x+w-7+k,y+k,8-k,1,15);ps_fill(fb,x,y+h-1-k,7-k,1,15);}
            ps_line(fb,x+w-7,y,x+w,y+7,1);ps_line(fb,x,y+h-7,x+7,y+h-1,1);
            ps_box(fb,split+4,y+18,10,10,5,1,false);
        }
    }
    int left=x+(style==PS_SHELF_CARD_DEFAULT?12:6);
    ps_line(fb,left,y+12,split-6,y+12,3);ps_line(fb,left,y+19,split-10,y+19,8);
}
static void shelf_card_picker_render(uint8_t *fb){
    if(!s_shelf_card_picker)return;
    shelf_card_picker_t r=shelf_card_picker_layout();
    ps_modal_background(fb);
    ps_box(fb,r.panel.x+3,r.panel.y+4,r.panel.width,r.panel.height,14,2,true);
    ps_box(fb,r.panel.x,r.panel.y,r.panel.width,r.panel.height,14,15,true);
    ps_text(fb,r.panel.x+18,r.panel.y+16,16,"上次阅读卡片",1);
    ps_text(fb,r.panel.x+18,r.panel.y+42,9,"选择书架续读区域的外观",5);
    const char *details[]={"书页签与进度副联","纵向开孔与副联","横向开孔与进度栏","切角与圆形进度章"};
    for(int i=0;i<PS_SHELF_CARD_STYLE_COUNT;i++){
        EpdRect row=r.option[i];
        ps_box(fb,row.x,row.y,row.width,row.height,9,i==ps_shelf_card_style()?2:11,true);
        ps_box(fb,row.x+1,row.y+1,row.width-2,row.height-2,8,15,true);
        if(i==ps_shelf_card_style())ps_icon(fb,row.x+10,row.y+12,14,PI_CHECK,1);
        ps_text_fixed(fb,row.x+32,row.y+11,14,shelf_card_style_name((uint8_t)i),1);
        ps_text_fixed(fb,row.x+12,row.y+41,10,details[i],5);
        shelf_card_motif(fb,row.x+row.width-80,row.y+12,62,44,(uint8_t)i);
        if(i==ps_shelf_card_style())ps_text_fixed(fb,row.x+12,row.y+61,9,"当前使用",5);
    }
    ps_button(fb,r.cancel.x,r.cancel.y,r.cancel.width,r.cancel.height,"取消",false);
}
static EpdRect device_about_header_rect(void) {
    ps_view_t v=ps_view();
    return v.landscape?(EpdRect){16,82,(v.w-44)/2,222}:
        (EpdRect){16,82,v.w-32,222};
}
static EpdRect device_update_channel_rect(int channel) {
    EpdRect header = device_about_header_rect();
    const int width = 50, gap = 6;
    return (EpdRect){header.x + header.width - 16 - 2 * width - gap + channel * (width + gap),
                     header.y + 76, width, 32};
}
static EpdRect device_guide_rect(void) {
    ps_view_t v=ps_view();
    return v.landscape?(EpdRect){16,310,(v.w-44)/2,34}:
        (EpdRect){16,576,v.w-32,34};
}
static EpdRect device_update_button_rect(void){
    EpdRect h=device_about_header_rect();return (EpdRect){h.x+16,h.y+166,h.width-32,40};
}
static EpdRect device_confirm_rect(void){
    ps_view_t v=ps_view();int w=v.landscape?500:v.w-32,h=266;
    return (EpdRect){(v.w-w)/2,(v.h-h)/2,w,h};
}
static void device_confirm_render(uint8_t *fb){
    if(!s_update_confirm)return;
    EpdRect r=device_confirm_rect();ps_modal_background(fb);ps_surface(fb,r.x,r.y,r.width,r.height,false);
    bool install=s_update_confirm==2;
    ps_text_fixed(fb,r.x+16,r.y+16,18,install?"安装系统更新":"切换测试频道",1);
    ps_wrap(fb,r.x+16,r.y+56,r.width-32,12,3,install?
        "安装会重启设备。请保持供电和 TF 卡插入，安装时不要关闭电源。":
        "测试版本可能存在功能异常或不稳定，适合愿意协助测试的用户。日常阅读建议使用稳定频道。",5);
    if(install){
        char line[80];snprintf(line,sizeof(line),"当前：%s",PS_VERSION);ps_text_fixed(fb,r.x+16,r.y+132,11,line,1);
        snprintf(line,sizeof(line),"目标：%s",s_update_confirm_version);ps_text_fixed(fb,r.x+16,r.y+156,11,line,1);
    }else ps_text_fixed(fb,r.x+16,r.y+150,11,"切换频道后仍需手动下载与安装。",1);
    int w=(r.width-44)/2,by=r.y+r.height-60;
    ps_button(fb,r.x+16,by,w,44,"取消",false);
    ps_button(fb,r.x+28+w,by,w,44,install?"安装并重启":"切换测试频道",true);
}
static PICO_STORE_COLD void device_about_render(uint8_t *fb){
    ps_view_t v=ps_view();int cw=v.landscape?(v.w-44)/2:v.w-32;
    ps_heading(fb,"关于本机",NULL);
    char update_latest[32], update_message[96];
    bool update_newer = false;
    (void)ps_updates_status(update_latest, sizeof(update_latest), &update_newer, NULL,
                            update_message, sizeof(update_message));
    EpdRect header=device_about_header_rect(),guide=device_guide_rect();
    ps_surface(fb,header.x,header.y,header.width,header.height,false);
    ps_text(fb,header.x+16,header.y+14,18,PS_OS_NAME,1);
    ps_text(fb,header.x+16,header.y+44,12,PS_RELEASE_LABEL,5);
    ps_line(fb,header.x+16,header.y+72,header.x+header.width-16,header.y+72,13);
    bool firmware_ready=ps_updates_firmware_ready(),firmware_busy=ps_updates_firmware_progress(NULL);
    ps_text(fb,header.x+16,header.y+84,14,"系统更新",1);
    uint8_t channel=ps_updates_channel();
    for(int i=0;i<2;i++){
        EpdRect r=device_update_channel_rect(i);
        bool selected=channel==(uint8_t)i;
        ps_box(fb,r.x,r.y,r.width,r.height,7,selected?1:12,true);
        ps_center(fb,r.x+r.width/2,r.y+9,10,ps_updates_channel_name((uint8_t)i),
                  selected?15:1);
    }
    char version[72];
    snprintf(version,sizeof(version),"%s · %s  →  %s",ps_updates_channel_name(channel),PS_VERSION,update_latest);
    ps_text(fb,header.x+16,header.y+112,10,version,update_newer?PS_THEME_SELECTED:PS_THEME_INK);
    char update_detail[128];
    if(channel==PS_UPDATE_CHANNEL_TEST)
        snprintf(update_detail,sizeof(update_detail),"测试频道可能不稳定 · %s",update_message);
    else
        snprintf(update_detail,sizeof(update_detail),"%s",update_message);
    ps_wrap(fb,header.x+16,header.y+134,header.width-32,11,1,update_detail,PS_THEME_SECONDARY);
    unsigned percent=0;
    if(ps_updates_firmware_progress(&percent)&&percent<=100){
        int px=header.x+16,py=header.y+156,pw=header.width-32;
        ps_box(fb,px,py,pw,5,2,13,true);
        if(percent)ps_box(fb,px,py,pw*percent/100,5,2,2,true);
    }
    const char *action=firmware_ready?"安装并重启":firmware_busy?"下载中":
        update_newer?"下载固件":"检查更新";
    ps_button(fb,header.x+16,header.y+166,header.width-32,40,action,
              firmware_ready||update_newer);
    char mac[24]="—", chip[48]="—", storage[48]="—", uid[40]="—";
#ifdef ESP_PLATFORM
    uint8_t m[6]={0};
    if(esp_read_mac(m,ESP_MAC_BASE)==ESP_OK)
        snprintf(mac,sizeof(mac),"%02X:%02X:%02X:%02X:%02X:%02X",m[0],m[1],m[2],m[3],m[4],m[5]);
    esp_chip_info_t ci;
    esp_chip_info(&ci);
    snprintf(chip,sizeof(chip),"ESP32-S3 · rev %u · %u 核",(unsigned)ci.revision,(unsigned)ci.cores);
    uint32_t flash=0;
    if(esp_flash_get_size(NULL,&flash)==ESP_OK) {
        size_t psram=heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
        snprintf(storage,sizeof(storage),"%u MB Flash · %u MB PSRAM",
                 (unsigned)(flash/(1024u*1024u)),(unsigned)(psram/(1024u*1024u)));
    }
    uint8_t pmu_uid[PMU_CHIP_UID_LEN];
    if(read_pico_pmu_uid_get(pmu_uid)==ESP_OK) {
        size_t o=0;
        for(size_t i=0;i<sizeof(pmu_uid)&&o+2<sizeof(uid);i++)
            o+=(size_t)snprintf(uid+o,sizeof(uid)-o,"%02X",pmu_uid[i]);
    }
#endif
    const char *labels[]={"系统版本","设备型号","显示规格","MAC 地址","芯片","存储","设备 UID"};
    const char *values[]={PS_VERSION,"小纸 Pico","16 灰阶 · 684×1216",mac,chip,storage,uid};
    int x=v.landscape?28+cw:16,y=v.landscape?82:316,rh=34;
    int panel_h=(int)(sizeof(labels)/sizeof(labels[0]))*rh+12;
    ps_surface(fb,x,y,cw,panel_h,false);
    for(size_t i=0;i<sizeof(labels)/sizeof(labels[0]);i++){
        int row=y+6+(int)i*rh;
        ps_text(fb,x+16,row+13,10,labels[i],5);
        ps_text_fixed(fb,x+108,row+12,11,values[i],1);
        if(i+1<sizeof(labels)/sizeof(labels[0]))ps_line(fb,x+16,row+rh,x+cw-16,row+rh,13);
    }
    ps_button(fb,guide.x,guide.y,guide.width,guide.height,"首次使用引导",false);
    device_confirm_render(fb);
}
static PICO_STORE_COLD void device_render(uint8_t* fb) {
    ps_view_t v=ps_view();ps_fill(fb,0,76,v.w,v.h-76,14);
    if(s_device_page==DEVICE_ABOUT){device_about_render(fb);return;}
    if(!s_device_page){
        ps_heading(fb,"系统设置",NULL);
        ps_surface(fb,16,82,v.w-32,56,false);
        ps_text(fb,28,90,16,PS_OS_NAME,1);
        ps_text(fb,28,116,10,PS_VERSION " · " PS_RELEASE_LABEL,5);
        ps_text(fb,v.w-60,98,12,"关于",5);
        for(int i=0;i<DEVICE_GROUP_COUNT;i++)device_row_render(fb,device_row_rect(i,true),device_groups[i].label,device_groups[i].detail,device_groups[i].icon,false);
        ps_center(fb,v.w/2,v.h-52,10,"轻点上方系统信息，查看关于本机",5);
        return;
    }
    const device_group_t *group=&device_groups[s_device_page];
    ps_heading(fb,group->label,"系统设置");
    for(int i=0;i<group->count;i++){
        const device_setting_t *item=&device_settings[group->first+i];
        char detail[64];
        const char *shown=item->detail;
        if(item->action==DEVICE_ACTION_STATUS_STYLE){
            snprintf(detail,sizeof(detail),"%s · 轻点切换",status_style_name(ps_status_style()));
            shown=detail;
        }else if(item->action==DEVICE_ACTION_SHELF_CARD){
            snprintf(detail,sizeof(detail),"%s · 轻点切换",shelf_card_style_name(ps_shelf_card_style()));
            shown=detail;
        }
        device_row_render(fb,device_row_rect(i,false),item->label,shown,item->icon,
                          item->action==DEVICE_ACTION_TEST_AWAKE);
    }
    status_style_picker_render(fb);
    shelf_card_picker_render(fb);
}
static int quick_catalog_row_height(void) {return ps_view().landscape?52:60;}
static int quick_catalog_rows(void) {return (ps_view().h-178-70)/quick_catalog_row_height();}
static EpdRect quick_catalog_rect(int row) {
    return (EpdRect){16,178+row*quick_catalog_row_height(),ps_view().w-32,quick_catalog_row_height()-4};
}
static PICO_STORE_COLD void quick_settings_render(uint8_t *fb) {
    if(!s_quick_mode){render_fan(fb);return;}
    ps_view_t v=ps_view();ps_heading(fb,"选择快捷功能","已在其他位置的功能会与此处交换");
    int catalog[QUICK_COUNT],catalog_n=quick_catalog(catalog);char text[100];
    int unplaced=0;for(int i=0;i<s_shortcuts.count;i++)if(quick_slot_of(s_quick_edit.slots,s_shortcuts.ids[i])<0)unplaced++;
    snprintf(text,sizeof(text),"位置 %d · 已放置 %d / 14 · 未放置 %d",s_quick_edit.selected+1,quick_placed(s_quick_edit.slots),unplaced);
    ps_text_fixed(fb,16,119,11,text,5);
    ps_button(fb,v.w-100,137,84,30,"移除此项",false);
    int rows=quick_catalog_rows();
    for(int j=0;j<rows&&s_quick_scroll+j<catalog_n;j++) {
        int id=catalog[s_quick_scroll+j],pos=quick_slot_of(s_quick_edit.slots,id);EpdRect row=quick_catalog_rect(j);int y=row.y;
        ps_box(fb,row.x,row.y,row.width,row.height,4,11,false);ps_icon(fb,24,y+10,20,s_quick[id].icon,2);
        ps_text_fixed(fb,54,y+7,13,s_quick[id].label,2);
        snprintf(text,sizeof(text),pos<0?(quick_enabled(id)?"未放置":"可添加"):"位置 %d · 交换",pos+1);
        ps_text_fixed(fb,54,y+28,10,text,5);ps_icon(fb,v.w-43,y+12,18,PI_PLUS,2);
    }
    ps_button(fb,16,v.h-68,80,30,"上一页",false);ps_button(fb,v.w-96,v.h-68,80,30,"下一页",false);
}
static PICO_STORE_COLD void onboarding_render(uint8_t* fb) {
    ps_view_t v = ps_view();
    int step = s_config.onboard_step;
    ps_text(fb, 18, 40, 12, "小纸 Pico  ·  首次使用", 5);
    ps_button(fb, v.w - 78, 31, 60, 32, "跳过", false);
    int center = v.w / 2;
    const char* titles[] = { "给阅读留一点时间", "习惯它的操作方式", "连接可以稍后再做",
                             "选择你喜欢的待机画面" };
    ps_icon_t icons[] = { PI_BOOK, PI_ROTATE, PI_WIFI, PI_CLOCK };
    ps_icon(fb, center - 27, 94, 54, icons[step], 2);
    ps_center(fb, center, 164, 24, titles[step], 2);
    if (step == 0) {
        ps_wrap(
            fb, 28, 214, v.w - 56, 15, v.landscape ? 2 : 5,
            "阅读是这里的主角。家庭控制与设备工具，始终从同一套快捷菜单进入。离线时也能安静阅读。",
            5);
        if (!v.landscape) {
            ps_button(fb, 28, 335, v.w - 56, 54, "阅读  ·  书库与阅读记录", false);
            ps_button(fb, 28, 400, v.w - 56, 54, "家庭  ·  房间与控制面板", false);
            ps_button(fb, 28, 465, v.w - 56, 54, "小游戏  ·  棋类与休闲", false);
        }
    }
    if (step == 1) {
        char t[96];
        snprintf(t, sizeof(t), "独立练习页  %d / 3", s_practice + 1);
        ps_center(fb, center, v.landscape ? 201 : 211, v.landscape ? 14 : 16, t, 2);
        ps_wrap(
            fb, 28, v.landscape ? 238 : 251, v.w - 56, v.landscape ? 12 : 14, v.landscape ? 2 : 4,
            "两侧实体键翻页；中键单击返回、双击切换首页、长按回书架。左右下角向内滑动选择快捷动作。",
            5);
    }
    if (step == 2) {
        int y = v.landscape ? 194 : 250, rh = v.landscape ? 30 : 58;
        ps_button(fb, 28, y, v.w - 56, rh, "Wi-Fi  ›", false);
        ps_button(fb, 28, y + rh + 8, v.w - 56, rh, "Home Assistant  ›", false);
        if (!v.landscape) ps_button(fb, 28, y + 2 * (rh + 8), v.w - 56, rh, "3D 打印机  ›", false);
    }
    if (step == 3) {
        int y = v.landscape ? 208 : 260, rh = v.landscape ? 35 : 65;
        ps_button(fb, 28, y, (v.w - 68) / 2, rh, "翻页时钟", !s_config.sleep_style);
        ps_button(fb, 40 + (v.w - 68) / 2, y, (v.w - 68) / 2, rh, "划线摘录", s_config.sleep_style);
        if (!v.landscape)
            ps_wrap(
                fb, 28, y + 88, v.w - 56, 13, 4,
                "摘录自动从所有划线中随机选择。休眠继承当前方向，也可以随时锁定方向。", 5);
    }
    for (int i = 0; i < 4; i++)
        ps_box(
            fb, center - 29 + i * 16, v.landscape ? 274 : v.h - 99, 9, 3, 1, i == step ? 2 : 11,
            true);
    ps_button(fb, 20, v.h - 79, v.w - 40, 39, step == 3 ? "开始阅读" : "下一步", true);
}

static bool dual_resume_visible(void) {
    return ps_dual_enabled() && ps_dual_peer_reading() && !app_book_global_is_open() &&
           home_route(s_route);
}
static EpdRect dual_resume_rect(void) {
    ps_view_t v = ps_view();
    // 避开左下/右下 48px 扇形起手区，避免悬浮按钮拦住角落滑动。
    // Keep clear of the 48px bottom-corner fan gesture zones.
    return (EpdRect){v.w - 178, v.h - 116, 158, 38};
}
static void render(app_ctx_t* ctx, uint8_t* fb) {
    (void)ctx;
    bool frozen=local_hold()&&!ps_usb_local_route(s_route);
    ps_view_t v = ps_view();
    memset(fb, 255, 1216 * 684 / 2);
    bool standby = s_route == PS_CLOCK || s_route == PS_QUOTES;
    // 两类快捷面板会覆盖整个内容区，跳过其下不可见页面的重复绘制。
    // Both shortcut overlays cover the entire content area; skip drawing the hidden page.
    if (!s_panel && !s_fan && !frozen) {
        if (reader_route(s_route))
            ps_reader_render(fb);
        else if (home_route(s_route))
            ps_home_render(fb);
        else if (s_route == PS_GAMES || s_route == PS_PLAYGAME || s_route == PS_ARCADEGAME || s_route == PS_APPLICATION)
            ps_games_render(fb);
        else if(s_route==PS_CLOUDFLARE&&!ps_desktop_app_available(true)) {
            ps_heading(fb,"网站流量",NULL);
            ps_icon(fb,v.w/2-26,v.landscape?103:203,52,PI_LINK,5);
            ps_center(fb,v.w/2,v.landscape?175:287,20,"尚无网站流量",1);
            ps_wrap(fb,32,v.landscape?217:333,v.w-64,14,3,"在多屏灵犀添加 Cloudflare 数据源并推送，流量信息会显示在这里。",5);
        }
        else if(s_route==PS_CANVAS||s_route==PS_CLOUDFLARE)ps_desktop_render_app(fb,s_route==PS_CLOUDFLARE);
        else if(s_route==PS_APPS)ps_launcher_render(fb);
        else if (s_route == PS_DEVICE)
            device_render(fb);
        else if (s_route == PS_WIFI || s_route == PS_TRANSFER || s_route == PS_WIRELESS_MANAGE)
            ps_connectivity_render(fb);
        else if (s_route == PS_STORAGE)
            ps_storage_render(fb);
        else if (s_route == PS_USB_TRANSFER)
            ps_usb_render(fb);
        else if (s_route == PS_BLUETOOTH)
            ps_bluetooth_render(fb);
        else if (s_route == PS_DATETIME) ps_datetime_render(fb);
        else if (s_route == PS_POWER) ps_power_render(fb);
        else if (s_route == PS_SOUND) ps_sound_render(fb);
        else if (s_route == PS_DUAL) ps_dual_render(fb);
        else if (s_route == PS_ANIMATION_SETTINGS) ps_animation_render(fb);
        else if (s_route == PS_NAV_SETTINGS) ps_navigation_render(fb);
        else if (s_route == PS_QUICK_SETTINGS)
            quick_settings_render(fb);
        else if (s_route == PS_ONBOARDING)
            onboarding_render(fb);
    }
    // 从设置打开的应用/游戏保留上级返回，不冒充首页导航。/ Apps/games opened from Settings keep parent Back instead of Home navigation.
    bool app_modal=s_route==PS_APPS&&ps_launcher_modal_active();
    if(!frozen&&!s_panel&&!s_fan&&!app_modal&&s_depth&&s_history[s_depth-1]==PS_DEVICE&&(s_route==PS_APPS||s_route==PS_GAMES)){
        ps_fill(fb,0,24,v.w,46,14);ps_heading(fb,"应用与服务",NULL);
    }
    if(frozen&&!s_panel&&!s_fan)ps_heading(fb,"TF 卡正由电脑管理","可继续使用设置和蓝牙，图书与应用暂停");
    if(!frozen&&!s_panel&&!s_fan&&!app_modal&&ps_home_launcher_visible())ps_tabs(fb,s_section);
    bool fullscreen_app=s_route==PS_CLOUDFLARE&&ps_desktop_app_available(true)&&!frozen&&!s_panel&&!s_fan;
    // 游戏页面占满屏幕，不绘制顶部系统状态栏，避免遮住棋盘/游戏区域。
    // Game pages own the whole screen; skip the system status bar so it cannot cover the board.
    bool game_fullscreen=s_route==PS_PLAYGAME||s_route==PS_ARCADEGAME||s_route==PS_APPLICATION;
    if (!standby && !fullscreen_app && !fan_editor_active() && !game_fullscreen) {
        ps_fill(fb, 0, 0, v.w, 24, 15);
        status_icons_draw(fb,v,ps_status_style()==PS_STATUS_STYLE_BOOK,ps_dual_connected());
        ps_notice_draw(fb, 7);
    }
    if (s_panel) render_panel(fb);
    if (s_fan) render_fan(fb);
    if(ps_usb_background()&&!s_panel&&!s_fan&&(frozen||s_route==PS_CANVAS||s_route==PS_CLOUDFLARE))ps_usb_background_render(fb);
    bool blocking_modal=app_modal||s_status_style_picker||s_shelf_card_picker||s_update_confirm;
    if (!standby&&!blocking_modal) ps_corner_controls(fb);
    if (dual_resume_visible() && !s_panel && !s_fan && !s_fan_candidate)
        ps_button(fb, dual_resume_rect().x, dual_resume_rect().y, dual_resume_rect().width,
                  dual_resume_rect().height, "回到双机阅读", true);
    if (!blocking_modal && !fan_frame_active() && s_toast[0] && ps_now() < s_toast_until) {
        int tw = v.w - 48;
        ps_box(fb, 24, v.h - 78, tw, 34, 5, 2, true);
        ps_center(fb, v.w / 2, v.h - 67, 11, s_toast, 15);
    }
}
static bool panel_event(const ps_event_t *e) {
    ps_view_t v=ps_view();int ids[QUICK_COUNT],n=group_items(ids),cols=v.landscape?3:2,rows=(v.h-220)/76,per=cols*rows;
    if(e->type==PS_SWIPE_L||e->type==PS_SWIPE_R){if(s_panel==1&&per){int pages=(n+per-1)/per;if(pages)s_group=(s_group+(e->type==PS_SWIPE_L?1:pages-1))%pages;}return true;}
    if(e->type!=PS_TAP)return true;
    if(e->y<76){s_panel=0;s_dirty=true;return true;}
    if(s_panel==3){for(int i=0;i<ps_navigation_count();i++){EpdRect r=panel_group_rect(i);if(ps_hit(e->x,e->y,r.x,r.y+38,r.width,r.height)){launcher_switch(i);return true;}}s_panel=0;s_dirty=true;return true;}
    for(int k=0;k<per&&s_group*per+k<n;k++){EpdRect r=panel_reading_rect(k);if(ps_hit(e->x,e->y,r.x,r.y+38,r.width,r.height)){quick_action(ids[s_group*per+k]);return true;}}
    if(e->y>=v.h-80){if(abs(e->x-v.w/2)<35){s_panel=0;ps_navigate(PS_QUICK_SETTINGS);}else{int pages=(n+per-1)/per;if(pages)s_group=(s_group+(e->x<v.w/2?pages-1:1))%pages;}s_dirty=true;}return true;
}
static PICO_STORE_COLD bool device_event(const ps_event_t* e) {
    ps_view_t v=ps_view();
    if(s_status_style_picker){
        status_style_picker_t layout=status_style_picker_layout();bool next=ps_status_percent();
        if(ps_switch_swipe(e,layout.percent.x+layout.percent.width-66,layout.percent.y,66,layout.percent.height,&next)){
            if(!ps_status_percent_set(next))ps_toast("保存失败，请重试");
            s_dirty=true;return true;
        }
        if(e->type==PS_TAP){
            status_style_picker_t r=status_style_picker_layout();
            if(ps_hit(e->x,e->y,r.percent.x,r.percent.y,r.percent.width,r.percent.height)){
                if(!ps_status_percent_set(!ps_status_percent()))ps_toast("保存失败，请重试");
                s_dirty=true;return true;
            }
            for(int i=0;i<PS_STATUS_STYLE_COUNT;i++){
                EpdRect row=r.option[i];
                if(ps_hit(e->x,e->y,row.x,row.y,row.width,row.height)){
                    if(ps_status_style_set((uint8_t)i)){
                        s_status_style_picker=false;
                        char message[64];
                        snprintf(message,sizeof(message),"已切换为%s",status_style_name((uint8_t)i));
                        ps_toast(message);
                        s_dirty=true;
                    }else ps_toast("外观设置保存失败");
                    return true;
                }
            }
            if(ps_hit(e->x,e->y,r.cancel.x,r.cancel.y,r.cancel.width,r.cancel.height)||
               !ps_hit(e->x,e->y,r.panel.x,r.panel.y,r.panel.width,r.panel.height)){
                s_status_style_picker=false;s_dirty=true;
            }
        }
        return true;
    }
    if(s_shelf_card_picker){
        if(e->type==PS_TAP){
            shelf_card_picker_t r=shelf_card_picker_layout();
            for(int i=0;i<PS_SHELF_CARD_STYLE_COUNT;i++){
                EpdRect row=r.option[i];
                if(ps_hit(e->x,e->y,row.x,row.y,row.width,row.height)){
                    if(ps_shelf_card_style_set((uint8_t)i)){
                        s_shelf_card_picker=false;
                        char message[64];
                        snprintf(message,sizeof(message),"已切换为%s",shelf_card_style_name((uint8_t)i));
                        ps_toast(message);
                        s_dirty=true;
                    }else ps_toast("外观设置保存失败");
                    return true;
                }
            }
            if(ps_hit(e->x,e->y,r.cancel.x,r.cancel.y,r.cancel.width,r.cancel.height)||
               !ps_hit(e->x,e->y,r.panel.x,r.panel.y,r.panel.width,r.panel.height)){
                s_shelf_card_picker=false;s_dirty=true;
            }
        }
        return true;
    }
    if(s_device_page==DEVICE_ABOUT){
        EpdRect firmware=device_update_button_rect(),guide=device_guide_rect();
        if(s_update_confirm){
            if(e->type!=PS_TAP)return true;
            EpdRect r=device_confirm_rect();int w=(r.width-44)/2,by=r.y+r.height-60;
            if(ps_hit(e->x,e->y,r.x+16,by,w,44)){s_update_confirm=0;s_dirty=true;return true;}
            if(ps_hit(e->x,e->y,r.x+28+w,by,w,44)){
                uint8_t action=s_update_confirm;s_update_confirm=0;
                if(action==1){
                    if(ps_updates_channel_set(PS_UPDATE_CHANNEL_TEST)){(void)ps_updates_check();ps_toast("已切换测试频道");}
                    else ps_toast("频道切换失败，请等待当前任务完成");
                }else{
                    char latest[33];
                    if(!ps_updates_firmware_version(latest,sizeof(latest))||strcmp(latest,s_update_confirm_version))ps_toast("固件状态已变化，请重新检查更新");
                    else if(!ps_updates_arm_firmware())ps_toast("固件未就绪或更新器不可用");
                }
                s_dirty=true;return true;
            }
            return true;
        }
        if(e->type==PS_TAP){
            for(int i=0;i<2;i++){
                EpdRect r=device_update_channel_rect(i);
                if(!ps_hit(e->x,e->y,r.x,r.y,r.width,r.height))continue;
                if(ps_updates_channel()==(uint8_t)i)return true;
                if(i==PS_UPDATE_CHANNEL_TEST){s_update_confirm=1;s_toast[0]=0;s_dirty=true;return true;}
                if(ps_updates_channel_set((uint8_t)i)){(void)ps_updates_check();ps_toast("已切换稳定频道");}
                else ps_toast("频道切换失败，请等待当前任务完成");
                s_dirty=true;return true;
            }
        }
        if(e->type==PS_TAP&&ps_hit(e->x,e->y,firmware.x,firmware.y,firmware.width,firmware.height)){
            if(ps_updates_firmware_ready()){
                char version[33];
                if(ps_updates_firmware_version(version,sizeof(version))){
                    snprintf(s_update_confirm_version,sizeof(s_update_confirm_version),"%s",version);
                    s_update_confirm=2;s_toast[0]=0;s_dirty=true;
                }
            }else if(!ps_updates_download_firmware()&&!ps_updates_check())ps_toast("请连接 Wi-Fi 或稍后重试");
            return true;
        }
        if(e->type!=PS_TAP)return false;
        if(ps_hit(e->x,e->y,guide.x,guide.y,guide.width,guide.height)){
            s_config.onboard_step=0;ps_navigate(PS_ONBOARDING);return true;
        }
        return false;
    }
    if(e->type==PS_SWIPE_L||e->type==PS_SWIPE_R){
        const device_group_t *g=&device_groups[s_device_page];
        for(int i=0;s_device_page&&i<g->count;i++)if(device_settings[g->first+i].action==DEVICE_ACTION_TEST_AWAKE){
            EpdRect r=device_row_rect(i,false);bool next;
            if(ps_switch_swipe(e,r.x+r.width-66,r.y,66,r.height,&next)){app_settings_set_test_awake(next);s_last_input=ps_now();s_dirty=true;return true;}
        }
    }
    if(e->type!=PS_TAP)return false;
    if(!s_device_page&&ps_hit(e->x,e->y,16,82,v.w-32,56)){s_device_page=DEVICE_ABOUT;return true;}
    const device_group_t *group=&device_groups[s_device_page];
    int count=s_device_page?group->count:DEVICE_GROUP_COUNT;
    for(int i=0;i<count;i++){
        EpdRect r=device_row_rect(i,!s_device_page);
        if(!ps_hit(e->x,e->y,r.x,r.y,r.width,r.height))continue;
        if(!s_device_page&&i){s_device_page=i;return true;}
        const device_setting_t *item=&device_settings[s_device_page?group->first+i:0];
        if(item->action==DEVICE_ACTION_STATUS_STYLE){
            s_status_style_picker=true;
            s_dirty=true;
        }else if(item->action==DEVICE_ACTION_SHELF_CARD){
            s_shelf_card_picker=true;
            s_dirty=true;
        }else if(item->action==DEVICE_ACTION_TEST_AWAKE){
            app_settings_set_test_awake(!app_settings_test_awake());s_last_input=ps_now();
            ps_toast(app_settings_test_awake()?"保持唤醒已开启":"已恢复自动休眠");
        }else ps_navigate(item->route);
        return true;
    }
    return false;
}
static PICO_STORE_COLD bool quick_settings_event(const ps_event_t *e) {
    ps_view_t v=ps_view();
    if(e->type==PS_CANCEL){quick_edit_contact_cancel();return true;}
    if(!s_quick_edit.active)return false;
    if(s_quick_mode) {
        int catalog[QUICK_COUNT],total=quick_catalog(catalog),rows=quick_catalog_rows();
        int direction=e->type==PS_SWIPE_L||e->type==PS_SWIPE_U?1:e->type==PS_SWIPE_R||e->type==PS_SWIPE_D?-1:0;
        if(e->type==PS_TAP&&e->y>=v.h-75)direction=e->x<v.w/2?-1:1;
        if(direction){s_quick_scroll+=direction*rows;if(s_quick_scroll>=total)s_quick_scroll=0;if(s_quick_scroll<0)s_quick_scroll=((total-1)/rows)*rows;return true;}
        if(e->type!=PS_TAP)return false;
        if(ps_hit(e->x,e->y,v.w-100,137,84,30)){quick_edit_assign(QUICK_EMPTY);s_quick_mode=0;return true;}
        for(int i=0;i<rows;i++) {
            EpdRect row=quick_catalog_rect(i);if(!ps_hit(e->x,e->y,row.x,row.y,row.width,row.height))continue;
            int at=s_quick_scroll+i;if(at>=total)return true;
            quick_edit_assign(catalog[at]);s_quick_mode=0;return true;
        }return false;
    }
    fan_item_t a[QUICK_COUNT];int n=fan_layout(a);
    if(e->type==PS_PRESS) {
        s_quick_edit.source=fan_pick(a,n,e->x,e->y,-1);s_quick_edit.target=s_quick_edit.source;
        s_quick_edit.suppress_tap=false;return s_quick_edit.source>=0;
    }
    if(e->type==PS_LONG&&s_quick_edit.source>=0) {
        s_quick_edit.dragging=true;s_quick_edit.suppress_tap=true;s_quick_edit.selected=s_quick_edit.source;s_fast=true;return true;
    }
    if(e->type==PS_MOVE&&s_quick_edit.dragging) {
        int target=fan_pick(a,n,e->x,e->y,s_quick_edit.target);
        bool changed=target!=s_quick_edit.target;s_quick_edit.target=target;if(changed)s_fast=true;return changed;
    }
    if(e->type==PS_RELEASE) {
        if(!s_quick_edit.dragging)return false;
        // UP必须重新判定；松手在安全区或屏幕外不会使用上一次悬停目标。
        // Re-pick UP: releasing in the safe area/outside must never reuse the previous hover target.
        int target=fan_pick(a,n,e->x,e->y,s_quick_edit.target),source=s_quick_edit.source;
        if(source>=0&&target>=0&&source!=target) {
            quick_edit_snapshot();uint8_t id=s_quick_edit.slots[source];s_quick_edit.slots[source]=s_quick_edit.slots[target];s_quick_edit.slots[target]=id;
            s_quick_edit.selected=target;
        }
        quick_edit_contact_cancel();s_fast=true;return true;
    }
    if(e->type!=PS_TAP||s_quick_edit.suppress_tap)return false;
    int ix=fan_prompt_x(),bw=v.landscape?80:76,gap=8;
    if(e->y>=178&&e->y<212) {
        if(ps_hit(e->x,e->y,ix,178,bw,34)){s_quick_edit.active=false;ps_back();return true;}
        if(ps_hit(e->x,e->y,ix+bw+gap,178,bw,34)) {
            if(s_quick_edit.undo_valid){uint8_t temp[FAN_SLOTS];memcpy(temp,s_quick_edit.slots,FAN_SLOTS);memcpy(s_quick_edit.slots,s_quick_edit.undo,FAN_SLOTS);memcpy(s_quick_edit.undo,temp,FAN_SLOTS);s_quick_edit.save_failed=false;}return true;
        }
        if(ps_hit(e->x,e->y,ix+2*(bw+gap),178,bw,34)) {
            if(quick_save_slots(s_quick_edit.slots)){s_quick_edit.active=false;ps_back();}
            else s_quick_edit.save_failed=true;
            return true;
        }
    }
    if(e->y>=220&&e->y<252) {
        if(ps_hit(e->x,e->y,ix,220,bw,32)){quick_edit_snapshot();memcpy(s_quick_edit.slots,s_quick_defaults,FAN_SLOTS);return true;}
        if(ps_hit(e->x,e->y,ix+bw+gap,220,bw,32)){s_quick_edit.side=!s_quick_edit.side;quick_edit_contact_cancel();return true;}
        if(ps_hit(e->x,e->y,ix+2*(bw+gap),220,bw,32)){quick_edit_assign(QUICK_EMPTY);return true;}
    }
    int at=fan_pick(a,n,e->x,e->y,-1);
    if(at>=0){s_quick_edit.selected=at;s_quick_mode=1;s_quick_scroll=0;return true;}
    return false;
}
static PICO_STORE_COLD bool onboarding_event(const ps_event_t* e) {
    ps_view_t v = ps_view();
    int step = s_config.onboard_step;
    if (e->type == PS_SWIPE_L && step == 1) {
        s_practice = (s_practice + 1) % 3;
        return true;
    }
    if (e->type == PS_SWIPE_R && step == 1) {
        s_practice = (s_practice + 2) % 3;
        return true;
    }
    if (e->type != PS_TAP) return false;
    if (ps_hit(e->x, e->y, v.w - 78, 31, 60, 32)) {
        s_config.onboard_done = 1;
        save_config();
        s_depth = 0;
        enter(PS_SHELF);
        return true;
    }
    if (e->y >= v.h - 79 && e->y < v.h - 36) {
        if (step < 3) {
            s_config.onboard_step++;
            save_config();
        } else {
            s_config.onboard_done = 1;
            save_config();
            s_depth = 0;
            enter(PS_SHELF);
        }
        return true;
    }
    if (step == 2) {
        int y = v.landscape ? 194 : 250, rh = v.landscape ? 30 : 58;
        if (e->y >= y && e->y < y + 3 * (rh + 8)) {
            int n = (e->y - y) / (rh + 8);
            if (n < 3) {
                ps_route_t r[] = { PS_WIFI, PS_HA_SETTINGS, PS_PRINTER_SETTINGS };
                ps_navigate(r[n]);
                return true;
            }
        }
    }
    if (step == 3 && e->y > 200 && e->y < v.h - 100) {
        ps_set_sleep_style(e->x > v.w / 2);
        return true;
    }
    return false;
}
static bool dispatch(const ps_event_t *e) {
    if(ps_storage_busy())return true;
    ps_view_t v=ps_view();bool standby=s_route==PS_CLOCK||s_route==PS_QUOTES;
    bool frozen=local_hold()&&!ps_usb_local_route(s_route);
    bool root=!frozen&&launcher_root();
    if(s_panel)return panel_event(e);
    if (dual_resume_visible() && e->type == PS_TAP && ps_hit(e->x, e->y, dual_resume_rect().x,
            dual_resume_rect().y, dual_resume_rect().width, dual_resume_rect().height)) {
        if (!resume_real_book()) ps_toast("另一台尚未就绪");
        return true;
    }
    if(!standby&&e->type==PS_TAP) {
        if(s_route!=PS_QUICK_SETTINGS&&(e->x<48||e->x>=v.w-48)&&e->y>=v.h-48){open_panel(1);return true;}
        if(root&&e->y>=28&&e->y<70){if(e->x>=v.w-46)ps_global_open_settings(s_ctx);else{int count=ps_navigation_count(),w=(v.w-72-(count-1)*4)/count,col=(e->x-12)/(w+4);if(e->x>=12&&col<count)launcher_switch(col);}return true;}
        bool heading=frozen||setting_route(s_route)||s_route==PS_RECORDS||s_route==PS_EDITOR||s_route==PS_IMPORT||
            s_route==PS_PLAYGAME||s_route==PS_ARCADEGAME||s_route==PS_APPLICATION||(s_route==PS_CLOUDFLARE&&!ps_desktop_app_available(true))||((s_route==PS_APPS||s_route==PS_GAMES)&&!root)||(home_route(s_route)&&!root);
        if(heading&&e->x>=0&&e->x<44&&e->y>=24&&e->y<76){ps_back();return true;}
    }
    if(root&&(e->type==PS_SWIPE_L||e->type==PS_SWIPE_R)) {int at=ps_navigation_index(s_route),next=at+(e->type==PS_SWIPE_L?1:-1);if(at>=0&&next>=0&&next<ps_navigation_count())launcher_switch(next);return true;}
    if(local_hold()){
        if(ps_usb_background()&&(s_route==PS_CANVAS||!ps_usb_local_route(s_route))&&ps_usb_event(e))return true;
        if(!ps_usb_local_route(s_route))return true;
    }
    if(s_global_app)return true;
    if(reader_route(s_route))return ps_reader_event(e);
    if(home_route(s_route))return ps_home_event(e);
    if(s_route==PS_APPS)return ps_launcher_event(e);
    if(s_route==PS_GAMES||s_route==PS_PLAYGAME||s_route==PS_ARCADEGAME||s_route==PS_APPLICATION)return ps_games_event(e);
    if(s_route==PS_DEVICE)return device_event(e);
    if(s_route==PS_WIFI||s_route==PS_TRANSFER||s_route==PS_WIRELESS_MANAGE)return ps_connectivity_event(e);
    if(s_route==PS_BLUETOOTH)return ps_bluetooth_event(e);
    if(s_route==PS_DATETIME)return ps_datetime_event(e);
    if(s_route==PS_POWER)return ps_power_event(e);
    if(s_route==PS_SOUND)return ps_sound_event(e);
    if(s_route==PS_DUAL)return ps_dual_event(e);
    if(s_route==PS_ANIMATION_SETTINGS)return ps_animation_event(e);
    if(s_route==PS_NAV_SETTINGS)return ps_navigation_event(e);
    if(s_route==PS_STORAGE)return ps_storage_event(e);
    if(s_route==PS_USB_TRANSFER)return ps_usb_event(e);
    if(s_route==PS_QUICK_SETTINGS)return quick_settings_event(e);
    if(s_route==PS_ONBOARDING)return onboarding_event(e);
    return false;
}
static void map_touch(int rawx, int rawy, int* x, int* y) {
    enum EpdRotation rotation = epd_get_rotation();
    // 正向画布先归一到原设备坐标；已验收的倒竖/倒横计算和实体键原样保留。
    // Normalize the other two canvases to the existing device coordinates; preserve the accepted inverted pair and raw physical keys.
    if ((rotation == EPD_ROT_PORTRAIT || rotation == EPD_ROT_LANDSCAPE) &&
        rawx >= 0 && rawx < 684 && rawy >= 0 && rawy < 1216) {
        rawx = 683 - rawx;
        rawy = 1215 - rawy;
    }
    if (ps_view().landscape) {
        *x = (1215 - rawy) * 640 / 1216;
        *y = rawx * 360 / 684;
    } else {
        *x = rawx * 360 / 684;
        *y = rawy * 640 / 1216;
    }
}
static void touch_tick(app_ctx_t* ctx) {
    const cst836u_touch_t* t = ctx->touch;
    if (!t || ctx->consumed || t->count > 1) {
        if (s_tracking) {
            ps_event_t e = { .type = PS_CANCEL };
            dispatch(&e);
        }
        if (s_fan || s_fan_candidate) s_dirty = true;
        s_fast = false;
        s_tracking = s_fan = s_fan_candidate = false;
        return;
    }
    int x, y;
    map_touch(t->x, t->y, &x, &y);
    ps_view_t v = ps_view();
    if(s_route==PS_EDITOR&&(ctx->pressed||ctx->released)){
        s_ink_gray_settle=false;
        if(s_ink_gray_due)s_ink_gray_due=ctx->now_ms+900;
    }
    if (ctx->pressed && t->y < 1300) {
        s_tracking = true;
        s_x0 = s_lastx = x;
        s_y0 = s_lasty = y;
        s_down_ms = ctx->touch_sample_ms?ctx->touch_sample_ms:ctx->now_ms;
        s_long = false;
        s_last_input = ctx->now_ms;
        s_fan_candidate = (s_route != PS_CLOCK && s_route != PS_QUOTES && s_route != PS_QUICK_SETTINGS) &&
                          (x < 48 || x >= v.w - 48) && y >= v.h - 48;
        if(!s_fan)s_fan_side = x >= v.w / 2;
        if (s_fan) {s_fan_candidate=false;fan_move(x,y);}
        else if (!s_fan_candidate) {
            ps_event_t e = { PS_PRESS, x, y, x, y, 0 };
            if (dispatch(&e)) s_dirty = true;
        }
    }
    if (!s_tracking) return;
    int dx = x - s_x0, dy = y - s_y0;
    int64_t held = (ctx->touch_sample_ms?ctx->touch_sample_ms:ctx->now_ms) - s_down_ms;
    if (t->touched) {
        if (s_fan_candidate && !s_fan && abs(dx) + abs(dy) > 12 && dy < 8 &&
            (s_fan_side ? dx < 8 : dx > -8)) {
            ps_games_action(5);
            s_fan = true;
            s_fan_selected = -1;
            s_panel = 0;
            s_dirty = true;
            s_fast = true;
        }
        if (s_fan) {
            fan_move(x, y);
        } else if (!s_fan_candidate) {
            bool ink=ps_reader_editor_ink_mode()||ps_apps_ink_mode();
            bool moved=ink?(x!=s_lastx||y!=s_lasty):
                abs(x-s_lastx)+abs(y-s_lasty)>3&&ctx->now_ms-s_move_ms>=70;
            if (moved) {
                ps_event_t e = { PS_MOVE, s_x0, s_y0, x, y, held };
                if (dispatch(&e)) s_dirty = true;
                s_move_ms = ctx->now_ms;
                s_lastx = x;
                s_lasty = y;
            }
            if (!s_long && held >= 500 && abs(dx) + abs(dy) < 13) {
                s_long = true;
                ps_event_t e = { PS_LONG, s_x0, s_y0, x, y, held };
                if (dispatch(&e)) s_dirty = true;
            }
        }
    }
    if (ctx->released) {
        s_tracking = false;
        s_last_input = ctx->now_ms;
        if (s_fan) {
            if(ps_hit(x,y,fan_prompt_x(),32,76,34)) {
                int side=s_fan_side;fan_present_reset();quick_action(10);s_quick_edit.side=side;s_dirty=true;
            } else if(ps_hit(x,y,v.w-48,24,48,48)){s_fan=s_fan_candidate=false;s_fan_selected=-1;s_dirty=true;}
            else fan_release(x,y);
            return;
        }
        if (s_fan_candidate) {
            s_fan_candidate=false;s_fan=true;s_fan_selected=-1;s_panel=0;
            ps_games_action(5);s_dirty=true;return;
        }
        ps_event_t e = { PS_RELEASE, s_x0, s_y0, x, y, held };
        ps_route_t previous_route = s_route;
        int previous_panel = s_panel;
        const app_desc_t *previous_legacy = s_legacy;
#ifdef ESP_PLATFORM
        const int64_t input_started=esp_timer_get_time();
#endif
        if (dispatch(&e)) {
            s_dirty = true;
            if(previous_route==PS_EDITOR&&s_route==PS_EDITOR)s_editor_render_due=ctx->now_ms+85;
        }
#ifdef ESP_PLATFORM
        if(previous_route==PS_EDITOR){
            int64_t elapsed=esp_timer_get_time()-input_started,age=ctx->touch_sample_ms?ctx->now_ms-ctx->touch_sample_ms:0;
            if(elapsed>s_editor_logic_max_us)s_editor_logic_max_us=elapsed;
            if(age>s_editor_capture_max_ms)s_editor_capture_max_ms=age;
            s_editor_input_count++;
        }
#endif
        // 抬手完成编辑后不把同一手指的合成点击传给新页面。/ Do not forward the synthesized tap to a page opened on release.
        if (s_route != previous_route || s_panel != previous_panel || s_legacy != previous_legacy) return;
        if (!s_long) {
            if (abs(dx) < 13 && abs(dy) < 13)
                e.type = PS_TAP;
            else if (abs(dx) >= 34 && abs(dx) > abs(dy))
                e.type = dx < 0 ? PS_SWIPE_L : PS_SWIPE_R;
            else if (abs(dy) >= 34 && abs(dy) > abs(dx))
                e.type = dy < 0 ? PS_SWIPE_U : PS_SWIPE_D;
            else
                e.type = PS_CANCEL;
            if (dispatch(&e)) s_dirty = true;
        }
    }
}
void ps_global_cancel_input(void) {
    ps_reader_editor_ink_cancel();ps_apps_ink_cancel();
    if(ps_ink_cancel())s_force_full=true;
    ps_apps_ink_invalidate();
    if(s_fan||(s_quick_edit.active&&(s_quick_edit.dragging||s_quick_edit.source>=0||s_quick_edit.target>=0)))s_overlay_restore=true;
    quick_edit_contact_cancel();
    s_center_down=s_center_hold=s_center_pending=s_center_second=false;s_center_app=NULL;
    s_tracking=s_fan=s_fan_candidate=s_global_claimed=false;s_fan_selected=-1;
}
bool ps_keyboard_capture_active(void){
    return s_ready&&s_global_enabled&&!s_global_app&&s_route==PS_EDITOR&&!s_panel&&!s_fan&&!s_fan_candidate&&
        !ps_usb_held()&&!ps_usb_busy()&&!s_content_owned&&!ps_storage_busy();
}
bool ps_power_partial_safe(void) { return !s_dirty&&!s_force_full&&!s_overlay_restore&&!ps_ink_pending(); }
void ps_global_open_settings(app_ctx_t *ctx) {
    if(ctx)s_ctx=ctx;
    ps_navigate(PS_DEVICE);
    if(s_route==PS_DEVICE)s_device_page=0;
}
static bool center_input(app_ctx_t *ctx,const app_desc_t *app) {
    const cst836u_touch_t *t=ctx->touch;
    bool center=t&&ui_key_hit_test(t->x,t->y)==UI_KEY_2;
    if(ctx->consumed||!t||t->count>1||ctx->request_app||ctx->request_return) {
        bool claim=t&&t->count>1&&(center||s_center_down||s_center_pending);
        ps_global_cancel_input();return claim;
    }
    if((s_center_down||s_center_pending)&&(s_center_app!=app||s_center_route!=s_route)){ps_global_cancel_input();return false;}
    if(ctx->pressed&&center) {
        s_last_input=ctx->now_ms;
        if(s_center_pending&&ctx->now_ms>s_center_due){s_center_pending=false;ps_back();return true;}
        s_center_second=s_center_pending;
        s_center_app=app;s_center_route=s_route;s_center_down=true;s_center_hold=false;s_center_since=ctx->now_ms;
        return true;
    }
    if(s_center_down) {
        if(t->touched&&!center){ps_global_cancel_input();return true;}
        if(t->touched&&!s_center_hold&&ctx->now_ms-s_center_since>=650){s_center_hold=true;s_center_pending=s_center_second=false;ps_switch(0);return true;}
        if(ctx->released||!t->touched){s_center_down=false;if(s_center_hold){s_center_hold=false;return true;}
            if(s_center_second){s_center_pending=s_center_second=false;open_panel(3);}
            else{s_center_pending=true;s_center_due=ctx->now_ms+300;}
        }return true;
    }
    if(s_center_pending&&ctx->pressed&&!center){s_center_pending=false;return false;}
    if(s_center_pending&&ctx->now_ms>=s_center_due){s_center_pending=false;ps_back();return true;}
    return s_center_pending;
}
void ps_global_enable(bool enabled) {
    ps_global_cancel_input();
    s_global_enabled = enabled;
    s_global_app = NULL;
    s_global_claimed = false;
    if (!enabled) s_book_return = false;
}
void ps_global_page(app_ctx_t* ctx, const app_desc_t* app) {
    s_ctx = ctx;
    const app_desc_t* next = s_global_enabled && app && !app->product_shell ? app : NULL;
    if (next == s_global_app) return;
    ps_global_cancel_input();
    s_global_app = next;
    s_overlay_restore=false;
    s_tracking = s_fan = s_fan_candidate = s_global_claimed = false;
    s_panel = 0;
    s_fan_selected = -1;
    s_global_redraw = APP_REDRAW_NONE;
    if (next == &app_book) s_real_book_preferred = true;
}
bool ps_global_input(app_ctx_t *ctx,const app_desc_t *app,app_redraw_t *redraw) {
    if(!s_global_enabled||!app||!ctx)return false;
    if(ctx->pressed||ctx->released||(ctx->touch&&ctx->touch->touched))ps_power_activity(ctx->now_ms);
    s_ctx=ctx;s_global_redraw=APP_REDRAW_NONE;
    if(ps_dual_service(ctx,app,redraw))return true;
    if(!app->product_shell&&ps_background_io_allowed()&&!ps_ink_pending())ps_maintenance_tick();
    if(content_tick())s_dirty=true;
    if(!local_hold()&&!app->product_shell&&ps_desktop_tick()){*redraw=APP_REDRAW_NONE;return true;}
    if(local_hold()){
        if(ps_usb_tick()||ps_bluetooth_tick())s_dirty=true;
        if(!ps_usb_busy())touch_tick(ctx);
        if(s_toast[0]&&ctx->now_ms>=s_toast_until){s_toast[0]=0;s_dirty=true;}
        bool was_dirty=s_dirty;s_dirty=false;
        if(center_input(ctx,app))was_dirty|=s_dirty;
        if(s_content_owned&&was_dirty)s_content_refresh=true;
        *redraw=was_dirty?APP_REDRAW_PAGE:APP_REDRAW_NONE;s_dirty=false;return true;
    }
    if(ps_bluetooth_input(ctx,app,s_panel||s_fan||s_tracking||s_center_down||s_center_pending,redraw)) {
        s_last_input=ctx->now_ms;ps_power_activity(ctx->now_ms);return true;
    }
    if(s_overlay_restore){s_overlay_restore=false;*redraw=APP_REDRAW_PAGE;return true;}
    bool was_dirty=s_dirty;s_dirty=false;
    if(center_input(ctx,app)){*redraw=s_global_redraw!=APP_REDRAW_NONE?s_global_redraw:s_dirty?APP_REDRAW_PAGE:APP_REDRAW_NONE;s_dirty|=was_dirty;return true;}
    s_dirty|=was_dirty;
    if(app->product_shell||app!=s_global_app||!ctx->touch)return false;
    const cst836u_touch_t *t=ctx->touch;
    if(t->y>=1216&&!s_global_claimed)return false;
    int x,y;map_touch(t->x,t->y,&x,&y);ps_view_t v=ps_view();
    bool corner=y>=v.h-48&&(x<48||x>=v.w-48);
    if(!s_global_claimed&&!s_panel&&!s_fan&&!(ctx->pressed&&corner))return false;
    if(!s_global_claimed&&app==&app_book)app_book_global_prepare();
    s_global_claimed=true;s_global_redraw=APP_REDRAW_NONE;s_dirty=false;touch_tick(ctx);
    *redraw=s_global_redraw!=APP_REDRAW_NONE?s_global_redraw:s_dirty?APP_REDRAW_PAGE:APP_REDRAW_NONE;
    if(ctx->released||t->count>1||(!t->touched&&!s_tracking))s_global_claimed=false;
    return true;
}
void ps_global_draw_controls(const app_desc_t *app,uint8_t *fb) {
    if(!s_global_enabled||app!=s_global_app)return;
    ps_view_t v=ps_view();ps_corner_controls(fb);
    if(s_toast[0]&&ps_now()<s_toast_until){ps_box(fb,24,v.h-78,v.w-48,34,5,2,true);ps_center(fb,v.w/2,v.h-67,11,s_toast,15);}
}
bool ps_global_present(app_ctx_t* ctx, const app_desc_t* app, app_redraw_t redraw) {
    if(local_hold()&&!s_panel&&!s_fan){
        if(redraw!=APP_REDRAW_NONE&&redraw!=APP_REDRAW_DONE){render(ctx,ctx->fb);guard_draw_result(ctx->hl,update_display_mode(ctx->hl,MODE_GL16));}
        return true;
    }
    if(!fan_frame_active())fan_present_reset();
    if (!s_global_enabled || app != s_global_app || (!s_panel && !s_fan)) return false;
    if (redraw == APP_REDRAW_NONE || redraw == APP_REDRAW_DONE) return true;
    if(s_fan)return fan_present(ctx);
    // 菜单全覆盖时无需绘制正文；不调用真实书的推屏成功回调。/ A full overlay skips the book and must not acknowledge its initial loading frame.
    render(ctx, ctx->fb);
    s_fast = false;
    guard_draw_result(ctx->hl,update_display_mode(ctx->hl,MODE_GL16));
    return true;
}
static void poll_hardware(void) {
    int64_t now = ps_now();
    if (!s_wifi_sample_ms || now-s_wifi_sample_ms>=5000) {
        s_wifi_sample_ms=now;
        read_pico_connectivity_link_t net={0};read_pico_connectivity_get_link(&net);
        uint8_t level=s_wifi_level;
        if(!net.online)level=0;
        else if(!level)level=net.rssi>=-60?3:net.rssi>=-75?2:1;
        else if(level==3&&net.rssi<-64)level=2;
        else if(level==2)level=net.rssi>-56?3:net.rssi<-79?1:2;
        else if(level==1&&net.rssi>-71)level=2;
        if(level!=s_wifi_level){s_wifi_level=level;s_dirty=true;}
    }
    if (now - s_pmu_ms >= 5000 || !s_pmu_ms) {
        s_pmu_ms = now;
        bool refreshed=read_pico_pmu_refresh()==ESP_OK;
        const pmu_snapshot_t* p = read_pico_pmu_get();
        if (p && p->status_ok) {
            s_battery = p->soc_permille <= 1000 ? (p->soc_permille + 5) / 10 : -1;
            s_charging = p->charge_state == PMU_CHARGE_CHARGING;
        }
        ps_sound_charge_sample(refreshed&&p&&p->status_ok,p?p->charge_state:0,now);
        if (refreshed && p) ps_datetime_sync_from_rtc(p->unix_sec,p->time_ok,p->time_synced);
    }
}
void ps_power_poll_hardware(app_ctx_t *ctx) { s_ctx=ctx;poll_hardware(); }
bool ps_power_dock_ready(void) {
    bool eligible=s_route==PS_SHELF||s_route==PS_READ||s_route==PS_RECORDS||
        s_route==PS_HA||s_route==PS_PRINTERS||s_route==PS_DEVICE;
    return eligible&&s_config.landscape&&ps_external_power()&&ps_home_local_network()&&
        ps_now()-s_motion_ms>=5000;
}
static void on_enter(app_ctx_t* ctx) {
    s_ctx = ctx;
    if (!s_ready) {
        book_store_root_t roots[2];
        int count = 0;
        if (book_store_roots(roots, &count) == ESP_OK && count) {
            int selected = 0;
            for (int i = 0; i < count; i++)
                if (roots[i].is_flash) {
                    selected = i;
                    break;
                }
            s_store_is_sd = !roots[selected].is_flash;
            snprintf(s_store_root, sizeof(s_store_root), "%s/.pico-system", roots[selected].path);
        }
        store_media_bind();
        memset(&s_config, 0, sizeof(s_config));
        if (!ps_load("system_v1", &s_config, sizeof(s_config)) || s_config.version != 1) {
            memset(&s_config, 0, sizeof(s_config));
            s_config.version = 1;
            s_config.quick = 8191;
            s_config.interval = 15;
            s_config.demo_ha = 1;
        }
        if (s_config.wifi_count > 8) s_config.wifi_count = 0;
        if (s_config.wifi_current >= s_config.wifi_count) s_config.demo_wifi = 0;
        if (s_config.onboard_step > 3) s_config.onboard_step = 0;
        s_config.quick &= 8191;
        quick_load();
        s_config.locked = !!s_config.locked;
        s_config.landscape = !!s_config.landscape;
        uint8_t guard = 1;
        if (ps_load("touchguard1", &guard, sizeof(guard))) s_touch_guard = guard ? 1 : 0;
        uint8_t status_style = PS_STATUS_STYLE_LINE;
        (void)ps_load("statusstyle1", &status_style, sizeof(status_style));
        s_status_style = status_style == PS_STATUS_STYLE_BOOK ? PS_STATUS_STYLE_BOOK : PS_STATUS_STYLE_LINE;
        uint8_t percent = status_style == 2;
        (void)ps_load("statuspct1", &percent, sizeof(percent));
        s_status_percent = percent != 0;
        uint8_t shelf_card_style = PS_SHELF_CARD_DEFAULT;
        if (ps_load("shelfcards1", &shelf_card_style, sizeof(shelf_card_style)) &&
            shelf_card_style < PS_SHELF_CARD_STYLE_COUNT) s_shelf_card_style = shelf_card_style;
        else s_shelf_card_style = PS_SHELF_CARD_DEFAULT;
        if(s_config.sleep_style>5)s_config.sleep_style=0;
        const int valid[] = { 5, 15, 30, 60, 360, 720 };
        bool interval_ok = false;
        for (int i = 0; i < 6; i++)
            if (s_config.interval == valid[i]) interval_ok = true;
        if (!interval_ok) s_config.interval = 15;
        for (int i = 0; i < 8; i++) {
            s_config.wifi[i].ssid[47] = 0;
            s_config.wifi[i].password[63] = 0;
        }
        ps_connectivity_init();
        ps_usb_init();
        ps_bluetooth_init();
        ps_datetime_init();
        ps_sound_init();
        ps_navigation_init();
        ps_reader_init();
        ps_home_init();
        ps_games_init();
        ps_apps_register_navigation();
        ps_desktop_init();
        ps_weread_init();
        read_pico_content_set_storage_hooks(content_acquire,content_release,content_cancel);
        s_ready = true;
        s_route = s_config.onboard_done ? PS_SHELF : PS_ONBOARDING;
#ifdef ESP_PLATFORM
        (void)esp_ota_mark_app_valid_cancel_rollback();
#endif
    }
    // 返回系统时恢复本次会话的完整方向；存档仍只保存横竖屏偏好。
    // Restore the full session orientation on return; persistence retains the existing landscape preference only.
    bool rotation_landscape = s_display_rotation == EPD_ROT_LANDSCAPE || s_display_rotation == EPD_ROT_INVERTED_LANDSCAPE;
    if (s_display_rotation < 0 || rotation_landscape != !!s_config.landscape)
        s_display_rotation = s_config.landscape ? EPD_ROT_INVERTED_LANDSCAPE : EPD_ROT_INVERTED_PORTRAIT;
    epd_set_rotation((enum EpdRotation)(ps_dual_enabled() ? ps_dual_rotation() : s_display_rotation));
    read_pico_connectivity_resume();
    ps_connectivity_enter(s_route);
    s_panel = 0;
    s_tracking = false;
    s_last_input = ctx->now_ms;
    ps_power_activity(ctx->now_ms);
    s_dirty = true;
    ps_reader_enter(s_route);
    ps_home_enter(s_route);
    ps_games_enter(s_route);
    if (ctx->sensor_ready) read_pico_sensor_wake(ctx->acc);
    poll_hardware();
}
static PICO_STORE_COLD void system_exit(app_ctx_t* ctx) {
    ps_global_cancel_input();
    s_network_resume_pending = false;
    // 先原子阻止新同步再停网；已有同步则保留日常 STA，让阅读子页不打断后台任务。
    // Block new sync admission before stopping networking; retain daily STA for an active job across reading subpages.
    bool weread_paused = ps_weread_pause();
    if (weread_paused) {
        while (!ps_connectivity_shutdown()) ESP_LOGW("pico_system", "waiting for transfer shutdown");
    }
    ps_reader_enter(PS_ROUTE_COUNT);
    ps_home_enter(PS_ROUTE_COUNT);
    ps_games_enter(PS_ROUTE_COUNT);
    if (ctx->sensor_ready) read_pico_sensor_sleep(ctx->acc);
    s_tracking = s_fan = false;
    epd_set_rotation(EPD_ROT_INVERTED_PORTRAIT);
    if (weread_paused) ps_weread_resume();
}
PICO_STORE_COLD void ps_media_lost(void) {
    ps_dual_suspend();
    if(!s_ready)return;
    ps_weread_cancel();
    ps_global_cancel_input();
    if (s_external_active) return;
    checkpoint_invalidate();
    if (s_store_is_sd) s_store_lost = true;
    while (!ps_connectivity_shutdown()) ESP_LOGW("pico_system", "waiting for transfer after media loss");
    ps_connectivity_media_lost();
    ps_reader_media_lost();
    ps_games_media_lost();
    read_pico_connectivity_resume();
    s_network_resume_pending = false;
    s_tracking = s_fan = s_fan_candidate = false;
    s_dirty = true;
}
static void system_media_lost(app_ctx_t* ctx) {
    (void)ctx;
    ps_media_lost();
}
static void system_media_restored(app_ctx_t* ctx) {
    (void)ctx;
    ps_media_restored();
}
static app_redraw_t on_tick(app_ctx_t* ctx) {
    s_ctx = ctx;
    // 落笔时延后可选轮询，避免同步I2C/后台文件访问打断笔迹。
    // Defer optional polling during ink capture so synchronous I2C/background files do not interrupt strokes.
    bool touch_down=ctx->touch&&ctx->touch->touched;
    bool ink_down=ps_reader_editor_ink_active()||((ps_reader_editor_ink_mode()||ps_apps_ink_mode())&&touch_down);
    bool editor_live=s_route==PS_EDITOR&&(touch_down||ctx->pressed||ctx->released||ctx->input_pending||ctx->now_ms-s_last_input<200);
    bool input_live=ink_down||editor_live||(ps_apps_ink_mode()&&touch_down)||ps_ink_pending();
    if (!input_live && s_network_resume_pending && read_pico_connectivity_resume() == ESP_OK) s_network_resume_pending = false;
    if(!input_live&&!touch_down)poll_hardware();
    if(!input_live)ps_maintenance_tick();
    if (ps_usb_tick()) s_dirty = true;
    // 电脑独占 TF 时仅运行无文件操作；后台全局输入继续拥有触摸。/ Run no file consumers while the host owns TF; global input retains background touch.
    if(ps_usb_held()||s_content_owned){if(!ps_usb_busy())touch_tick(ctx);s_last_input=ctx->now_ms;return s_dirty?(s_dirty=false,APP_REDRAW_PAGE):APP_REDRAW_NONE;}
    if (!input_live) {
        if (ps_connectivity_tick()) s_dirty = true;
        if (ps_bluetooth_tick()) s_dirty = true;
        if (ps_datetime_tick()) s_dirty = true;
        if (ps_updates_tick()) s_dirty = true;
    }
    if (ps_storage_tick()) s_dirty = true;
    if (ps_storage_busy() || ps_usb_busy()) {
        s_last_input = ctx->now_ms;
        return s_dirty ? (s_dirty = false, APP_REDRAW_PAGE) : APP_REDRAW_NONE;
    }
    touch_tick(ctx);
    if ((!editor_live||s_route!=PS_EDITOR)&&ps_reader_tick()) s_dirty = true;
    if (!input_live && ps_home_tick()) {
        s_dirty = true;
        if (s_route == PS_CLOCK) s_fast = true;
    }
    if (!input_live && ps_games_tick()) s_dirty = true;
    if (!input_live && ps_desktop_tick()) s_dirty = true;
    if (!input_live && ctx->sensor_ready && ctx->now_ms - s_acc_ms >= 500) {
        s_acc_ms = ctx->now_ms;
        sc7a20h_sample_t sample;
        if (read_pico_accel_read(ctx->acc, &sample) == ESP_OK) {
            bool asleep = s_route == PS_CLOCK || s_route == PS_QUOTES;
            int movement = s_sample_valid ? abs(sample.x_mg - s_last_sample.x_mg) +
                                                abs(sample.y_mg - s_last_sample.y_mg) +
                                                abs(sample.z_mg - s_last_sample.z_mg)
                                          : 0;
            s_last_sample = sample;
            s_sample_valid = true;
            if (movement > 160) s_motion_ms = ctx->now_ms;
            // BSP 已换成设备轴，不再交换；自动目标保留四向符号。/ BSP already returns device axes; do not swap again, and retain all four pose signs.
            int orientation = -1;
            if (!asleep && (ps_dual_enabled() ? (!ps_dual_rotation_locked() && ps_dual_role() == 1)
                                              : !ps_rotation_locked()) &&
                !s_tracking && !s_panel && !ctx->consumed && !(ctx->touch && ctx->touch->touched))
                orientation = sample_rotation(&sample);
            rotation_consider(orientation);
        } else {
            s_orientation_candidate = -1;
            s_orientation_matches = 0;
        }
    }
    if (s_toast[0] && ctx->now_ms >= s_toast_until) {
        s_toast[0] = 0;
        ps_reader_editor_invalidate();
        s_dirty = true;
    }
    if (s_legacy) {
        ctx->request_app = s_legacy;
        s_legacy = NULL;
        return APP_REDRAW_NONE;
    }
    // 余相排空且停止触摸后才定稿；连续输入不插入灰阶波形。/ Settle only after phases and touch stop; never interrupt continuous typing.
    if(s_ink_gray_due&&!touch_down&&!ctx->input_pending&&!ps_ink_pending()&&ctx->now_ms>=s_ink_gray_due){s_ink_gray_settle=true;s_dirty=true;}
    // 输入逻辑已完成；短暂停顿后合并文本/候选绘制，队列未排空时不启动另一轮同步扫描。
    // Logical keys are already committed; merge text/candidate paint after a short pause and drain queued edges before another synchronous scan.
    if(s_route==PS_EDITOR&&!s_panel&&!s_fan&&!s_toast[0]&&!s_force_full&&
       (touch_down||ctx->input_pending||ctx->now_ms<s_editor_render_due))return APP_REDRAW_NONE;
    if (s_dirty) {
        s_dirty = false;
        return APP_REDRAW_PAGE;
    }
    if(ps_ink_pending()){
        if(ps_ink_step(ctx))return APP_REDRAW_DONE;
        ps_ink_cancel();s_force_full=true;return APP_REDRAW_PAGE;
    }
    return APP_REDRAW_NONE;
}
static app_redraw_t on_key(app_ctx_t *ctx,int key) {
    s_ctx=ctx;if(ps_storage_busy()||ps_usb_busy())return APP_REDRAW_NONE;
    ps_power_activity(ctx->now_ms);
    s_last_input=ctx->now_ms;s_tracking=s_fan=s_fan_candidate=false;
    // 中键由全局去抖状态机拥有，左右键仍保留已验收翻页。/ The global recognizer owns center; side keys retain accepted paging.
    if(key==1)return APP_REDRAW_NONE;
    if(s_panel){int ids[QUICK_COUNT],n=s_panel==3?ps_navigation_count():enabled(ids);if(n)s_focus=(s_focus+(key==0?n-1:1))%n;return APP_REDRAW_PAGE;}
    if(s_route==PS_ONBOARDING&&s_config.onboard_step==1){s_practice=(s_practice+(key==0?2:1))%3;return APP_REDRAW_PAGE;}
    bool changed=reader_route(s_route)?ps_reader_key(key):home_route(s_route)?ps_home_key(key):s_route==PS_APPS?ps_launcher_key(key):(s_route==PS_GAMES||s_route==PS_PLAYGAME||s_route==PS_ARCADEGAME||s_route==PS_APPLICATION)?ps_games_key(key):false;
    return changed?APP_REDRAW_PAGE:APP_REDRAW_NONE;
}
static app_redraw_t on_key_long(app_ctx_t *ctx,int key) { (void)ctx;(void)key;return APP_REDRAW_NONE; }
static app_redraw_t on_lock(app_ctx_t* ctx) {
    ps_power_toggle(ctx,&app_pico_system);
    return APP_REDRAW_NONE;
}
static bool editor_present(app_ctx_t *ctx) {
#ifdef ESP_PLATFORM
    const int64_t started=esp_timer_get_time();
    ttf_bench_stats_t font_stats={0};ttf_bench_begin();
#endif
    EpdRect areas[4];int count=0;
    bool partial=s_route==PS_APPLICATION?ps_apps_ink_render_partial(ctx->fb,areas,&count):ps_reader_editor_render_partial(ctx->fb,areas,&count);
#ifdef ESP_PLATFORM
    ttf_bench_end(&font_stats);
    const int64_t painted=esp_timer_get_time();
#endif
    if(!partial)return false;
    if(!count)return ps_ink_step(ctx);
    for(int i=0;i<count;i++)fan_area_pixels(ctx->fb,NULL,areas[i],true);
    EpdRect damage=fast_damage_pixels(areas,count);
#ifdef ESP_PLATFORM
    const int64_t thresholded=esp_timer_get_time();
#endif
    s_ink_gray_due=ctx->now_ms+900;
    // 黑白差分中未变化像素不驱动；多处改字合成一次扫描，避免提交候选扫三遍。
    // Unchanged binary pixels hold; combine changed fields into one scan per candidate commit.
    if(!ps_ink_present(ctx,damage)){
        ps_ink_cancel();ps_reader_editor_invalidate();ps_apps_ink_invalidate();
        enum EpdDrawError error=update_display_full(ctx->hl);
        if(error!=EPD_DRAW_SUCCESS)s_force_full=true;
        guard_draw_result(ctx->hl,error);
    }
#ifdef ESP_PLATFORM
    // 只输出耗时与计数，不记录输入文字/密码；限速避免诊断反过来阻塞触摸采样。
    // Log only durations/counters, never input/password text; rate-limit so diagnostics cannot block touch capture.
    static int64_t last_log;
    if(s_route==PS_EDITOR&&ctx->now_ms-last_log>=1000){
        ESP_LOGI("editor_perf","keys=%u input_max_us=%lld capture_age_max_ms=%lld paint_us=%lld font_read_us=%lld font_raster_us=%lld misses=%lu threshold_us=%lld phase_us=%lld",
            s_editor_input_count,(long long)s_editor_logic_max_us,(long long)s_editor_capture_max_ms,
            (long long)(painted-started),(long long)font_stats.read_us,(long long)font_stats.raster_us,
            (unsigned long)font_stats.misses,(long long)(thresholded-painted),(long long)(esp_timer_get_time()-thresholded));
        last_log=ctx->now_ms;s_editor_input_count=0;s_editor_logic_max_us=s_editor_capture_max_ms=0;
    }
#endif
    return true;
}
static bool present(app_ctx_t* ctx, app_redraw_t redraw) {
    static bool reference_valid, last_landscape;
    static bool content_reference;
    static ps_route_t last_route;
    static enum EpdRotation last_rotation;
    enum EpdRotation rotation = epd_get_rotation();
    bool ink_frame=!s_panel&&!fan_frame_active()&&(s_route==PS_EDITOR||ps_apps_ink_mode());
    if(redraw==APP_REDRAW_FULL){ps_reader_editor_ink_cancel();ps_apps_ink_cancel();}
    if(!ink_frame||redraw==APP_REDRAW_FULL){if(ps_ink_cancel())s_force_full=true;s_ink_gray_due=0;s_ink_gray_settle=false;}
    if((ink_frame||s_route==PS_CANVAS||s_route==PS_CLOUDFLARE)&&redraw==APP_REDRAW_FULL){s_force_full=true;redraw=APP_REDRAW_PAGE;}
    if (redraw != APP_REDRAW_PAGE) {
        if(redraw==APP_REDRAW_FULL)ps_reader_editor_invalidate();
        if(redraw==APP_REDRAW_FULL||!fan_frame_active())fan_present_reset();
        last_rotation=rotation;
        last_landscape=s_config.landscape;
        reference_valid=true;
        if(redraw!=APP_REDRAW_NONE&&redraw!=APP_REDRAW_DONE)
            content_reference=redraw==APP_REDRAW_FULL&&!s_panel&&!fan_frame_active();
        last_route=s_route;
        s_fast = false;
        return false;
    }
    bool orientation_changed=!reference_valid || last_landscape!=s_config.landscape || last_rotation!=rotation;
    last_rotation=rotation;
    last_landscape=s_config.landscape;
    reference_valid=true;
    if(fan_frame_active()&&!orientation_changed&&!s_force_full){content_reference=false;return fan_present(ctx);}
    fan_present_reset();
    const int64_t started_us = esp_timer_get_time();
    const bool fast_requested = s_fast;
    bool force_full=s_force_full; s_force_full=false;
    const bool clean_content=!s_panel&&!fan_frame_active();
    const bool content_partial=content_reference&&last_route==s_route&&clean_content&&!orientation_changed&&!force_full;
    const bool editor_frame=s_route==PS_EDITOR&&clean_content;
    if(!editor_frame||orientation_changed||force_full)ps_reader_editor_invalidate();
    bool drawing_frame=s_route==PS_APPLICATION&&clean_content&&ps_apps_ink_mode();
    if(!drawing_frame||orientation_changed||force_full)ps_apps_ink_invalidate();
    if((editor_frame||drawing_frame)&&content_partial&&!s_ink_gray_settle&&!s_toast[0]&&editor_present(ctx)){
        s_fast=false;last_route=s_route;return true;
    }
    if(ps_ink_cancel())force_full=true;
    content_reference=false;
    // 稳定控件保留真实灰阶，不能因所属页面不是书架就把层次阈值化。
    // Preserve real grayscale on stable controls instead of thresholding every non-shelf page.
    bool moving_clock=s_route==PS_CLOCK && s_fast && ps_home_animating();
    bool moving_snake=s_route==PS_ARCADEGAME && ps_games_is_snake() && !ps_games_paused() && !s_panel;
    bool monochrome = !orientation_changed && !force_full && (moving_clock || moving_snake);
    s_fast = false;
    render(ctx, ctx->fb);
    const int64_t rendered_us = esp_timer_get_time();
    s_ink_gray_due=0;s_ink_gray_settle=false;
    if (monochrome) {
        // DU仅二值化变化像素；静止工具栏与背景保留真实灰阶。
        // Threshold only changed DU targets; retain native gray on static controls/background.
        size_t bytes = (size_t)epd_width() * epd_height() / 2;
        for (size_t i=0;i<bytes;i++) {
            uint8_t value=ctx->fb[i];
            uint8_t old=ctx->hl->back_fb[i],target=(value&0x80?0xf0:0)|(value&0x08?0x0f:0);
            ctx->fb[i]=((value&0xf0)==(old&0xf0)?value:target)&0xf0;
            ctx->fb[i]|=((value&0x0f)==(old&0x0f)?value:target)&0x0f;
        }
    }
    const int64_t thresholded_us = esp_timer_get_time();
    // 稳定页面用实际差分限制驱动与周期清理；底层仍整帧扫描，不承诺扫描耗时缩短。
    // Stable-page differences bound drive and periodic cleanup; the driver still scans a full frame.
    enum EpdDrawError e = (orientation_changed||force_full) ? update_display_full(ctx->hl) :
        content_partial?update_display_changed_mode(ctx->hl,monochrome?MODE_DU:MODE_GL16):
        update_display_mode(ctx->hl, monochrome ? MODE_DU : MODE_GL16);
    int fallback_error = -1;
    if (e != EPD_DRAW_SUCCESS) fallback_error = update_display_full(ctx->hl);
    if(s_route==PS_CANVAS||s_route==PS_CLOUDFLARE)ps_desktop_presented(e==EPD_DRAW_SUCCESS||fallback_error==EPD_DRAW_SUCCESS);
    content_reference=clean_content&&(e==EPD_DRAW_SUCCESS||fallback_error==EPD_DRAW_SUCCESS);
    last_route=s_route;
    const int64_t refreshed_us = esp_timer_get_time();
    int diff_ms, draw_ms, copy_ms;
    epd_hl_last_timing(&diff_ms, &draw_ms, &copy_ms);
    // req 是请求模式；周期 GC16 升级由共享显示出口另记。高层值可能来自此前的非空更新。
    // req precedes shared periodic GC16 promotion; last_hl values may describe an earlier nonempty update.
    ESP_LOGI(
        "pico_system",
        "present route=%d panel=%d fan=%d fast=%d landscape=%d req=%s "
        "render_us=%lld threshold_us=%lld refresh_us=%lld total_since_ctx_now_ms=%lld "
        "epd_err=%d fallback_err=%d last_hl_diff_ms=%d last_hl_draw_ms=%d last_hl_copy_ms=%d",
        (int)s_route, s_panel, s_fan, fast_requested, s_config.landscape,
        orientation_changed ? "rotationGC16" : force_full ? "standbyGC16" : (monochrome ? "DU" : "GL16"),
        (long long)(rendered_us - started_us),
        (long long)(monochrome ? thresholded_us - rendered_us : 0),
        (long long)(refreshed_us - thresholded_us),
        (long long)(refreshed_us / 1000 - ctx->now_ms), (int)e, fallback_error,
        diff_ms, draw_ms, copy_ms);
    return true;
}
const app_desc_t app_pico_system = { .title = "小纸 Pico",
                                     .detail = "书架 · 家庭 · 小游戏",
                                     .product_shell = true,
                                     .owns_keys = true,
                                     .custom_menu_handle = true,
                                     .manages_fonts = true,
                                     .on_media_lost = system_media_lost,
                                     .on_media_restored = system_media_restored,
                                     .blocks_idle_sleep = true,
                                     .enter_full = true,
                                     .on_enter = on_enter,
                                     .on_exit = system_exit,
                                     .render = render,
                                     .present = present,
                                     .on_key = on_key,
                                     .on_key_long = on_key_long,
                                     .on_tick = on_tick,
                                     .on_lock = on_lock };
