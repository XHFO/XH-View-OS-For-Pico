/* SPDX-License-Identifier: Apache-2.0
 * 中文：旧棋类源码的TF模块适配；所有系统服务均经ABI函数表。
 * English: TF-module adapter for legacy game code; all system services use the ABI table.
 * 冻结：不链接IDF或宿主内部符号；导航仅改变模块内叶页面。
 * Frozen: No IDF or host-internal imports; navigation changes only module leaf views.
 */
#pragma once
#include "../../components/read_pico_apps/include/pico_app_sdk.h"
#ifndef PICO_GAME_KIND
#error PICO_GAME_KIND must select one game from 0 to 5
#endif
#if PICO_GAME_KIND < 0 || PICO_GAME_KIND > 5
#error Unsupported PICO_GAME_KIND
#endif
#ifndef PICO_GAME_ID
#if PICO_GAME_KIND == 0
#define PICO_GAME_ID "gomoku"
#elif PICO_GAME_KIND == 1
#define PICO_GAME_ID "go"
#elif PICO_GAME_KIND == 2
#define PICO_GAME_ID "xiangqi"
#elif PICO_GAME_KIND == 3
#define PICO_GAME_ID "chess"
#elif PICO_GAME_KIND == 4
#define PICO_GAME_ID "2048"
#else
#define PICO_GAME_ID "snake"
#endif
#endif

static const pico_app_host_t *pga_host;
typedef pico_app_view_t ps_view_t;
typedef pico_app_event_t ps_event_t;
typedef enum { PS_GAMES,PS_PLAYGAME,PS_ARCADEGAME,PS_ROUTE_COUNT } ps_route_t;
enum { PS_PRESS=PGA_PRESS,PS_MOVE=PGA_MOVE,PS_RELEASE=PGA_RELEASE,PS_TAP=PGA_TAP,
       PS_LONG=PGA_LONG,PS_SWIPE_L=PGA_SWIPE_L,PS_SWIPE_R=PGA_SWIPE_R,
       PS_SWIPE_U=PGA_SWIPE_U,PS_SWIPE_D=PGA_SWIPE_D,PS_CANCEL=PGA_CANCEL };
static ps_route_t pga_route=PS_GAMES;
static ps_view_t ps_view(void){return pga_host->view();}
static ps_route_t ps_route(void){return pga_route;}
static void ps_navigate(ps_route_t route);
static bool ps_hit(int x,int y,int left,int top,int w,int h){return x>=left&&y>=top&&x<left+w&&y<top+h;}
#define ps_dirty() pga_host->dirty()
#define ps_toast(text) pga_host->toast(text)
#define ps_load(key,out,bytes) pga_host->load(key,out,bytes)
#define ps_store(key,data,bytes) pga_host->store(key,data,bytes)
#define ps_fill pga_host->fill
#define ps_line pga_host->line
#define ps_box pga_host->box
#define ps_text pga_host->text
#define ps_center pga_host->center
#define ps_wrap pga_host->wrap
#define ps_button pga_host->button
#define ps_heading pga_host->heading
// 兼容旧六包前缀；新包读取尾部双机接口。
// Preserve the legacy prefix; new packages read the dual-game tail.
static int32_t pga_duo_role(void){return pga_host->bytes>=offsetof(pico_app_host_t,duo_role)&&pga_host->duo_role?pga_host->duo_role():PGA_DUO_NONE;}
static bool pga_duo_send(uint8_t input){return pga_host->bytes>=offsetof(pico_app_host_t,duo_role)&&pga_host->duo_input_send&&pga_host->duo_input_send(input);}
static int32_t pga_duo_recv(void){return pga_host->bytes>=offsetof(pico_app_host_t,duo_role)&&pga_host->duo_input_recv?pga_host->duo_input_recv():-1;}
static bool pga_duo_packet_send(const void *data,size_t bytes){return pga_host->bytes>=offsetof(pico_app_host_t,duo_packet_send)&&pga_host->duo_packet_send&&pga_host->duo_packet_send(data,bytes);}
static int32_t pga_duo_packet_recv(void *data,size_t capacity){return pga_host->bytes>=offsetof(pico_app_host_t,duo_packet_send)&&pga_host->duo_packet_recv?pga_host->duo_packet_recv(data,capacity):-1;}
#define snprintf pga_host->format

// 编译器生成的结构体复制也必须走宿主，避免隐式libc导入。/ Compiler-generated copies also use the host, avoiding implicit libc imports.
__attribute__((visibility("hidden"))) void *memcpy(void *dst,const void *src,size_t n){return pga_host->copy(dst,src,n);}
__attribute__((visibility("hidden"))) void *memmove(void *dst,const void *src,size_t n){return pga_host->move(dst,src,n);}
__attribute__((visibility("hidden"))) void *memset(void *dst,int byte,size_t n){return pga_host->set(dst,byte,n);}
__attribute__((visibility("hidden"))) int memcmp(const void *a,const void *b,size_t n){return pga_host->compare(a,b,n);}
__attribute__((visibility("hidden"))) void bzero(void *dst,size_t n){pga_host->set(dst,0,n);}
static int pga_abs(int value){return value<0?-value:value;}
#define abs pga_abs

typedef void *TaskHandle_t;
#define pdPASS 1
#define MALLOC_CAP_SPIRAM 0u
#define MALLOC_CAP_8BIT 0u
#define esp_random() pga_host->random()
#define esp_timer_get_time() pga_host->time_us()
#define vTaskDelay(ms) pga_host->delay_ms((uint32_t)(ms))
#define vTaskSuspend(task) pga_host->worker_suspend()
#define uxTaskGetStackHighWaterMark(task) pga_host->worker_stack_free()
#define vTaskDeleteWithCaps(task) pga_host->worker_delete(task)
#define xTaskCreateWithCaps(fn,name,stack,arg,priority,out,caps) pga_host->worker_create(fn,arg,out)
static void ps_games_action(int action);
static void ps_games_enter(ps_route_t route);
