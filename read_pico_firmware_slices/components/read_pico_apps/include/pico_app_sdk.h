/* SPDX-License-Identifier: Apache-2.0
 * 中文：TF应用 ABI v1；仅固定宽度事件及显式宿主接口，不暴露板级或系统结构。
 * English: TF app ABI v1; fixed-width events and explicit host services, without board or system internals.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>

#define PICO_APP_ABI 1u
#define PICO_APP_MAX_STATE 65536u
#define PICO_APP_WORKER_STACK 20480u

enum { PGA_GOMOKU, PGA_GO, PGA_XIANGQI, PGA_CHESS, PGA_2048, PGA_SNAKE };
enum { PGA_DRAWING=128, PGA_WEREAD_KIND=129, PGA_HOME_LINK=130 };
enum { PGA_PAGE_LAUNCHER, PGA_PAGE_BOARD, PGA_PAGE_ARCADE, PGA_PAGE_APP };
enum { PGA_PRESS, PGA_MOVE, PGA_RELEASE, PGA_TAP, PGA_LONG, PGA_SWIPE_L,
       PGA_SWIPE_R, PGA_SWIPE_U, PGA_SWIPE_D, PGA_CANCEL };
enum { PGA_STATUS_ARCADE=1u, PGA_STATUS_SNAKE=2u, PGA_STATUS_PAUSED=4u,
       PGA_STATUS_BUSY=8u, PGA_STATUS_UNSAVED=16u, PGA_STATUS_INK=32u };
typedef struct { int32_t w,h; bool landscape; } pico_app_view_t;
typedef struct { int32_t type,x0,y0,x,y; int64_t ms; } pico_app_event_t;

/// 微信读书可选服务仅公开状态，不向TF模块暴露API Key。/ Optional WeRead services expose status only, never API Keys to TF modules.
enum { PGA_WEREAD_STATUS, PGA_WEREAD_SYNC, PGA_WEREAD_OPEN_SETUP };
enum { PGA_WEREAD_CONFIGURED=1u, PGA_WEREAD_WIFI=2u, PGA_WEREAD_BUSY=4u };
enum { PGA_WEREAD_IDLE, PGA_WEREAD_SHELF, PGA_WEREAD_SYNCING, PGA_WEREAD_DONE, PGA_WEREAD_ERROR, PGA_WEREAD_QUERYING };
typedef struct {
    uint32_t bytes,flags,state,revision,books,notes,progress,total;
    char error[24];
} pico_app_weread_status_t;

/// 固定只读查询；凭据及HTTPS属于宿主，模块只消费有界JSON。/ Fixed read-only queries; the host owns credentials/HTTPS and modules consume bounded JSON.
enum { PGA_WEREAD_SEARCH=1, PGA_WEREAD_INFO, PGA_WEREAD_CHAPTERS,
       PGA_WEREAD_PROGRESS, PGA_WEREAD_RECOMMEND, PGA_WEREAD_SIMILAR,
       PGA_WEREAD_STATS, PGA_WEREAD_HIGHLIGHTS, PGA_WEREAD_REVIEWS };
enum { PGA_WEREAD_WEEKLY, PGA_WEREAD_MONTHLY, PGA_WEREAD_ANNUALLY, PGA_WEREAD_OVERALL };
enum { PGA_WEREAD_INPUT_OPEN, PGA_WEREAD_INPUT_TAKE, PGA_WEREAD_INPUT_CANCEL };
typedef struct {
    uint32_t bytes,kind,scope,count,mode,review_type;
    int64_t cursor,synckey,chapter_uid,base_time;
    char book_id[64],keyword[160],session_id[128];
} pico_app_weread_query_t;
typedef struct {
    uint32_t bytes,request_id,revision,kind,total_bytes;
    bool busy,ready;
    char error[24];
} pico_app_weread_query_status_t;

typedef struct {
    uint32_t abi,bytes;
    pico_app_view_t (*view)(void);
    int32_t (*page)(void);
    void (*navigate)(int32_t page);
    void (*dirty)(void);
    void (*toast)(const char *text);
    bool (*load)(const char *key,void *out,size_t bytes);
    bool (*store)(const char *key,const void *data,size_t bytes);
    // 仅迁移宿主既有的games/arcade1存档，不允许任意路径。/ Only migrate legacy games/arcade1 saves, never arbitrary paths.
    bool (*legacy_load)(const char *key,void *out,size_t bytes);
    int64_t (*time_us)(void);
    uint32_t (*random)(void);
    void (*fill)(uint8_t*,int,int,int,int,int);
    void (*line)(uint8_t*,int,int,int,int,int);
    void (*box)(uint8_t*,int,int,int,int,int,int,bool);
    void (*text)(uint8_t*,int,int,int,const char*,int);
    void (*center)(uint8_t*,int,int,int,const char*,int);
    void (*wrap)(uint8_t*,int,int,int,int,int,const char*,int);
    void (*button)(uint8_t*,int,int,int,int,const char*,bool);
    void (*heading)(uint8_t*,const char*,const char*);
    // 每应用最多一项工作任务；必须停止后才可卸载代码。/ At most one worker per app; stop it before unmapping code.
    int (*worker_create)(void (*fn)(void*),void *arg,void **handle);
    void (*worker_delete)(void *handle);
    void (*worker_suspend)(void);
    uint32_t (*worker_stack_free)(void);
    void (*delay_ms)(uint32_t ms);
    void (*diagnostic)(const char *event,int type,unsigned generation,unsigned stack_free);
    int (*format)(char*,size_t,const char*,...);
    void *(*copy)(void*,const void*,size_t);
    void *(*move)(void*,const void*,size_t);
    void *(*set)(void*,int,size_t);
    int (*compare)(const void*,const void*,size_t);
    /// 可选v1尾部；先检查bytes；命令只提交异步工作，绘制不得调用。/ Optional v1 tail: check bytes; submit work asynchronously, never call while rendering.
    bool (*weread)(uint32_t command,pico_app_weread_status_t *status);
    /// 可选只读扩展；NULL请求仅查状态，结果读取必须携带原request_id。/ Optional read-only extension; NULL requests query status and reads require the original request_id.
    bool (*weread_query)(const pico_app_weread_query_t *request,pico_app_weread_query_status_t *status);
    bool (*weread_query_read)(uint32_t request_id,uint32_t offset,void *out,size_t capacity,size_t *read);
    /// OPEN传初值，TAKE仅在确认后返回一次；最多159字节，无模块回调。/ OPEN supplies initial text; TAKE returns a confirmed value once, at most 159 bytes, without module callbacks.
    bool (*weread_input)(uint32_t command,char *value,size_t capacity);
    /// 仅为官方阅读链接打开宿主二维码；返回保持模块状态。/ Open a host QR for an official reader link, retaining the module on return.
    bool (*weread_open_link)(const char *url);
    /// 仅绘制同一已完成查询的封面缓存；无网络/文件访问。/ Paint the completed query's cached cover only, without network or file access.
    bool (*weread_cover)(uint32_t request_id,uint8_t *fb,int x,int y,int width,int height);
} pico_app_host_t;

/// 旧八款应用所需的原始宿主前缀；追加能力不得改变ABI1。/ Original host prefix for the eight existing apps; optional additions preserve ABI1.
#define PICO_APP_HOST_V1_BYTES offsetof(pico_app_host_t,weread)

typedef struct {
    uint32_t abi,bytes,version,kind;
    const char *id;
    bool (*start)(const pico_app_host_t *host);
    void (*render)(uint8_t *framebuffer);
    bool (*event)(const pico_app_event_t*);
    bool (*key)(int key);
    bool (*tick)(void);
    void (*action)(int action);
    uint32_t (*status)(void);
    bool (*save)(void);
    // 返回前必须取消并join所有worker；不允许留下指向模块内存的宿主回调。
    // Cancel and join every worker before returning; leave no host callback pointing into module memory.
    void (*stop)(void);
    /// 可选v1尾部；-1需整帧，0无变化，1..4个设计坐标损伤区域；仅画待定新笔迹，不做文件IO。
    /// Optional v1 tail: -1 needs a full frame, 0 unchanged, or 1..4 design-coordinate damage boxes; draw pending ink without file IO.
    int32_t (*ink)(uint8_t *framebuffer,int32_t rects[4][4]);
} pico_app_exports_t;

/// 六旧游戏包仅包含此前缀；可选尾部必须检查bytes后读取。/ The six legacy game packages expose this prefix; check bytes before optional-tail reads.
#define PICO_APP_EXPORTS_V1_BYTES offsetof(pico_app_exports_t,ink)

// ELF入口及主机动态库共用注册函数。/ Shared registration entry for device ELF and native host libraries.
typedef const pico_app_exports_t *(*pico_app_query_t)(void);
