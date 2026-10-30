/*
 * SPDX-License-Identifier: Apache-2.0
 * 小纸 Pico 的 TF 检测、读写验证和显式格式化流程。
 * Read Pico TF detection, read/write verification and explicit formatting flow.
 * 冻结：普通界面两次确认后才清卡；可信主循环维护请求可在明确授权后跳过确认。
 * 不自动格式化或重试；先排空文件消费者；render仅绘制缓存的真实状态和阶段。
 * Frozen: UI erase needs two confirmations; explicitly authorized trusted main-loop maintenance may bypass them.
 * Never autoformat/retry; drain consumers first; render only paints cached real states and stages.
 */
#include "pico_system.h"
#include "pico_reader.h"
#include "read_pico_sd.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

enum { SD_OVERVIEW, SD_BACKUP, SD_ERASE, SD_RESULT, SD_RECORDS, SD_IMPORT_PROGRESS, SD_IMPORT_RESULT };
enum { SD_CHECK=1, SD_FORMAT=2 };
enum { SD_WORK_REMOUNT, SD_WORK_VERIFY, SD_WORK_FORMAT };
enum { SD_DETECT, SD_OPEN_FORMAT, SD_CANCEL, SD_CONTINUE, SD_CONFIRM, SD_DONE, SD_IMPORT };
typedef struct { int id,x,y,w,h; const char *label; bool primary; } sd_control_t;
static read_pico_sd_info_t sd_info,sd_confirm;
static int sd_view,sd_action,sd_work;
static bool sd_running,sd_started,sd_success;
static atomic_int sd_done;
static esp_err_t sd_result;
static int64_t sd_poll_at,sd_start_at,sd_confirm_at;
static char sd_message[160];
static char sd_records_current[160],sd_records_other[160];
static bool sd_records_available,sd_records_current_available,sd_records_current_is_sd;
static read_pico_sd_info_t sd_records_card;
static ps_reader_records_import_result_t sd_import_result;

bool ps_storage_busy(void){return sd_running;}
static bool sd_same_card(const read_pico_sd_info_t *a,const read_pico_sd_info_t *b){
    return a->present&&b->present&&a->media_generation==b->media_generation&&
        a->physical_bytes==b->physical_bytes&&a->capacity_bytes==b->capacity_bytes&&
        !memcmp(a->name,b->name,sizeof(a->name));
}
static bool sd_detecting(void){return sd_info.state==READ_PICO_SD_STATE_DETECTING||sd_info.error==ESP_ERR_NOT_FINISHED;}
static bool sd_verified(void){return sd_info.present&&sd_info.mounted&&sd_info.error==ESP_OK&&sd_info.read_verified&&sd_info.write_verified;}
static void sd_update(void){memset(&sd_info,0,sizeof(sd_info));sd_info.error=read_pico_sd_get_info(&sd_info);sd_poll_at=ps_now();}
static void sd_amount(char *out,size_t cap,uint64_t bytes){
    if(!bytes)snprintf(out,cap,"暂不可读取");
    else if(bytes>=1000000000ULL)snprintf(out,cap,"%.1f GB",(double)bytes/1000000000.0);
    else snprintf(out,cap,"%.0f MB",(double)bytes/1000000.0);
}
static void sd_capacity(char *out,size_t cap,const read_pico_sd_info_t *info){
    char total[32],free_space[32];sd_amount(total,sizeof(total),info->capacity_bytes?info->capacity_bytes:info->physical_bytes);
    if(info->mounted){if(info->free_bytes)sd_amount(free_space,sizeof(free_space),info->free_bytes);else snprintf(free_space,sizeof(free_space),"0 MB");}
    else snprintf(free_space,sizeof(free_space),"未挂载");
    snprintf(out,cap,"容量 %s · 可用 %s",total,free_space);
}
static const char *sd_state_title(void){
    if(!sd_info.present)return "未插入 TF 卡";
    if(sd_detecting())return "正在检测卡片";
    if(sd_info.state==READ_PICO_SD_STATE_EXPORTED)return "电脑正在使用 TF 卡";
    if(sd_info.state==READ_PICO_SD_STATE_UNSUPPORTED_FILESYSTEM)return "暂不支持此卡的格式";
    if(sd_info.state==READ_PICO_SD_STATE_NEEDS_REMOUNT)return "请重新检测 TF 卡";
    if(sd_info.state==READ_PICO_SD_STATE_FILESYSTEM_ERROR)return "文件系统无法读取";
    if(sd_info.state==READ_PICO_SD_STATE_IO_ERROR)return "卡片读写出现错误";
    if(sd_info.state==READ_PICO_SD_STATE_CHECK_FAILED)return "读写验证未通过";
    if(sd_verified())return "TF 卡可读写";
    if(sd_info.mounted)return "TF 卡已挂载";
    return "卡片暂不可用";
}
static const char *sd_state_hint(void){
    if(!sd_info.present)return "插入卡片后，选择重新检测。";
    if(sd_detecting())return "正在识别卡片，请稍候。";
    if(sd_info.state==READ_PICO_SD_STATE_EXPORTED)return "请先在电脑安全弹出，再结束 USB 传输。";
    if(sd_info.state==READ_PICO_SD_STATE_UNSUPPORTED_FILESYSTEM)return "请先备份文件，再选择设备格式化。";
    if(sd_info.state==READ_PICO_SD_STATE_NEEDS_REMOUNT)return "卡片状态已改变，重新检测后继续。";
    if(sd_verified())return "挂载、写入、同步与读回检查均已通过。";
    if(sd_info.mounted)return "选择重新检测，完成读写验证。";
    return "先重新检测；仍失败时可用电脑检查卡片。";
}
static const char *sd_filesystem(void){return sd_info.filesystem==READ_PICO_SD_FS_FAT?"FAT":sd_info.filesystem==READ_PICO_SD_FS_EXFAT?"exFAT":"未识别";}
static const char *sd_stage_title(void){
    if(!sd_started)return sd_work==SD_WORK_VERIFY?"准备检查读写":"已暂停文件访问";
    switch(sd_info.stage){
        case READ_PICO_SD_STAGE_DETECTING:return "正在识别卡片";
        case READ_PICO_SD_STAGE_MOUNTING:return "正在挂载文件系统";
        case READ_PICO_SD_STAGE_FORMATTING:return "正在重建文件系统";
        case READ_PICO_SD_STAGE_REMOUNTING:return "正在重新挂载";
        case READ_PICO_SD_STAGE_CHECKING_READ:return "正在检查文件读取";
        case READ_PICO_SD_STAGE_CHECKING_WRITE:return "正在写入并读回验证";
        case READ_PICO_SD_STAGE_COMPLETE:return "检查完成，正在整理结果";
        case READ_PICO_SD_STAGE_FAILED:return "操作未完成，正在整理结果";
        default:return sd_work==SD_WORK_FORMAT?"正在准备格式化":sd_work==SD_WORK_VERIFY?"正在检查读写":"正在重新检测";
    }
}
static int sd_controls(sd_control_t out[2]){
    if(sd_running||sd_view==SD_IMPORT_PROGRESS)return 0;
    ps_view_t v=ps_view();int y=v.landscape?276:526,h=v.landscape?32:44,w=(v.w-44)/2;
    if(sd_view==SD_RECORDS){
        out[0]=(sd_control_t){SD_CANCEL,16,y,w,h,"返回存储",false};
        if(!sd_records_available)return 1;
        out[1]=(sd_control_t){SD_IMPORT,28+w,y,w,h,"合并笔记",true};
    }else if(sd_view==SD_BACKUP||sd_view==SD_ERASE){
        out[0]=(sd_control_t){SD_CANCEL,16,y,w,h,"取消，保留文件",false};
        out[1]=(sd_control_t){sd_view==SD_BACKUP?SD_CONTINUE:SD_CONFIRM,28+w,y,w,h,sd_view==SD_BACKUP?"已备份，继续":"清空并格式化",sd_view==SD_ERASE};
    }else if(sd_view==SD_RESULT){
        out[0]=(sd_control_t){SD_DETECT,16,y,w,h,"重新检测",false};
        out[1]=(sd_control_t){SD_DONE,28+w,y,w,h,"返回存储",true};
    }else if(sd_view==SD_IMPORT_RESULT){
        out[0]=(sd_control_t){SD_DONE,16,y,w,h,"返回存储",true};
        return 1;
    }else{
        out[0]=(sd_control_t){SD_DETECT,16,y,w,h,"重新检测",true};
        out[1]=(sd_control_t){SD_OPEN_FORMAT,28+w,y,w,h,"格式化…",false};
    }
    return 2;
}

static void sd_records_open(void){
    sd_update();sd_records_card=sd_info;
    sd_records_current_available=ps_storage_record_roots(sd_records_current,sd_records_other,sizeof(sd_records_current),&sd_records_current_is_sd);
    sd_records_available=sd_records_current_available&&sd_records_other[0];
    sd_view=SD_RECORDS;
}
static bool sd_records_ready(void){
    char current[160],other[160];bool is_sd=false;
    sd_update();
    return sd_records_available&&sd_same_card(&sd_records_card,&sd_info)&&sd_info.mounted&&
        ps_storage_record_roots(current,other,sizeof(current),&is_sd)&&
        !strcmp(current,sd_records_current)&&!strcmp(other,sd_records_other)&&is_sd==sd_records_current_is_sd;
}
static void sd_records_finish(void){
    if(!sd_records_ready()){
        memset(&sd_import_result,0,sizeof(sd_import_result));sd_import_result.status=PS_READER_IMPORT_UNAVAILABLE;
    }else (void)ps_reader_records_import(sd_records_other,&sd_import_result);
    switch(sd_import_result.status){
        case PS_READER_IMPORT_OK:
            snprintf(sd_message,sizeof(sd_message),"新增 %u 条 · 跳过重复 %u 条 · 当前共 %u 条。源库保留。",(unsigned)sd_import_result.added,(unsigned)sd_import_result.duplicates,(unsigned)sd_import_result.total_count);break;
        case PS_READER_IMPORT_NO_SOURCE:snprintf(sd_message,sizeof(sd_message),"另一处存储没有可合并的笔记。当前记录保留。");break;
        case PS_READER_IMPORT_CORRUPT:snprintf(sd_message,sizeof(sd_message),"原有记录读取异常，未覆盖任何记录。请先检查存储。");break;
        case PS_READER_IMPORT_LIMIT:snprintf(sd_message,sizeof(sd_message),"合并后超过1024条，未合并。请先导出并移除不需要的记录。");break;
        case PS_READER_IMPORT_MEMORY:snprintf(sd_message,sizeof(sd_message),"可用内存不足，未合并。原记录保留，请重启后再试。");break;
        case PS_READER_IMPORT_SAVE_FAILED:snprintf(sd_message,sizeof(sd_message),"目标存储写入失败，未完成合并。原记录保留。");break;
        case PS_READER_IMPORT_BUSY:snprintf(sd_message,sizeof(sd_message),"请先保存正在编辑的笔记，再合并记录。");break;
        default:snprintf(sd_message,sizeof(sd_message),"存储状态已改变或暂不可用，未合并。请检查TF卡后重新进入。");break;
    }
    sd_view=SD_IMPORT_RESULT;
}

void ps_storage_enter(void){if(sd_running)return;sd_view=SD_OVERVIEW;sd_message[0]=0;sd_update();}
bool ps_storage_back(void){if(sd_running||sd_view==SD_IMPORT_PROGRESS)return true;if(sd_view!=SD_OVERVIEW){sd_view=SD_OVERVIEW;return true;}return false;}
static void sd_worker(void *unused){
    (void)unused;
    sd_result=sd_work==SD_WORK_FORMAT?read_pico_sd_format():sd_work==SD_WORK_VERIFY?read_pico_sd_verify():read_pico_sd_remount();
    if(sd_work==SD_WORK_REMOUNT&&sd_result==ESP_ERR_NOT_FINISHED)sd_result=ESP_OK;
    atomic_store_explicit(&sd_done,1,memory_order_release);vTaskDelete(NULL);
}
static bool sd_start(int action){
    if(sd_running)return false;
    sd_action=action;sd_work=action==SD_FORMAT?SD_WORK_FORMAT:SD_WORK_REMOUNT;
    sd_started=false;sd_success=false;sd_view=SD_OVERVIEW;sd_message[0]=0;sd_start_at=ps_now();
    atomic_store_explicit(&sd_done,0,memory_order_relaxed);
    if(ps_storage_prepare()){sd_running=true;return true;}
    sd_running=false;sd_view=SD_RESULT;
    snprintf(sd_message,sizeof(sd_message),"无法暂停文件访问，未开始存储操作。请稍后重试。");return false;
}
static bool sd_can_format(void){
    if(!sd_info.present){ps_toast("请先插入 TF 卡");return false;}
    if(sd_detecting()){ps_toast("请等待卡片检测完成");return false;}
    if(sd_info.state==READ_PICO_SD_STATE_EXPORTED){ps_toast("请先结束 USB 传输");return false;}
    if(sd_info.state==READ_PICO_SD_STATE_NEEDS_REMOUNT){ps_toast("请先重新检测卡片，再确认格式化");return false;}
    return true;
}
// 仅可信主循环接收明确授权后调用；协议worker不得直接进入维护。 / Only a trusted main-loop request after explicit authorization may call this; never enter from a protocol worker.
bool ps_storage_request_format(void){if(sd_running)return false;sd_update();return sd_can_format()&&sd_start(SD_FORMAT);}
static void sd_finish(void){
    bool verified=sd_result==ESP_OK&&sd_verified();
    sd_running=false;sd_started=false;sd_success=false;sd_view=SD_RESULT;ps_storage_resume();
    if(verified){
        sd_success=ps_storage_rebind(sd_action==SD_FORMAT);
        snprintf(sd_message,sizeof(sd_message),"%s",sd_success?(sd_action==SD_FORMAT?"格式化完成，读写验证通过。":"检测完成，读写验证通过。"):
                 "读写验证通过，笔记存储尚未启用。卡内已有数据时请重启读取。");
    }else if(sd_action==SD_FORMAT){
        snprintf(sd_message,sizeof(sd_message),"%s",sd_info.format_started?"格式化未完成，卡内文件可能已被清除。未自动重试，请先重新检测。":"格式化未开始或未完成。未自动重试，请检查卡片后重新检测。");
    }else snprintf(sd_message,sizeof(sd_message),"检测或读写验证未完成。卡内文件未被格式化，请查看状态后重新检测。");
}
bool ps_storage_tick(void){
    if(sd_view==SD_IMPORT_PROGRESS){sd_records_finish();return true;}
    if(sd_running){
        if(!sd_started){
            sd_started=true;
            if(xTaskCreate(sd_worker,"pico_sd_ui",6144,NULL,4,NULL)!=pdPASS){sd_result=ESP_ERR_NO_MEM;atomic_store_explicit(&sd_done,1,memory_order_release);}
            return true;
        }
        read_pico_sd_info_t before=sd_info;
        bool done=atomic_load_explicit(&sd_done,memory_order_acquire)!=0;
        if(done||ps_now()-sd_poll_at>=250)sd_update();
        bool changed=memcmp(&before,&sd_info,sizeof(sd_info))!=0;
        if(!done)return changed;
        if(sd_work==SD_WORK_REMOUNT&&sd_result==ESP_OK){
            if(sd_detecting()&&ps_now()-sd_start_at<30000)return changed;
            if(sd_info.mounted&&sd_info.error==ESP_OK){
                sd_work=SD_WORK_VERIFY;sd_started=false;atomic_store_explicit(&sd_done,0,memory_order_relaxed);return true;
            }
        }
        sd_finish();return true;
    }
    if(ps_route()!=PS_STORAGE||ps_now()-sd_poll_at<1000)return false;
    read_pico_sd_info_t before=sd_info;sd_update();
    if((sd_view==SD_BACKUP||sd_view==SD_ERASE)&&!sd_same_card(&sd_confirm,&sd_info)){
        sd_view=SD_OVERVIEW;snprintf(sd_message,sizeof(sd_message),"卡片状态已改变，请重新检测后再操作。");
    }
    return memcmp(&before,&sd_info,sizeof(sd_info))!=0;
}
static void sd_render_status(uint8_t *fb){
    ps_view_t v=ps_view();int x=v.landscape?316:16,y=v.landscape?86:254,w=v.landscape?308:v.w-32;
    ps_box(fb,16,v.landscape?86:84,v.landscape?280:v.w-32,v.landscape?156:154,6,10,false);
    ps_icon(fb,28,v.landscape?98:98,26,sd_verified()?PI_CHECK:PI_IMPORT,2);
    ps_text(fb,64,v.landscape?98:98,18,sd_state_title(),2);
    ps_wrap(fb,28,138,v.landscape?252:v.w-56,13,v.landscape&&sd_message[0]?4:2,v.landscape&&sd_message[0]?sd_message:sd_state_hint(),5);
    ps_box(fb,x,y,w,148,6,10,false);
    char line[120];sd_capacity(line,sizeof(line),&sd_info);ps_wrap(fb,x+12,y+12,w-24,13,2,line,3);
    snprintf(line,sizeof(line),"文件系统：%s",sd_filesystem());ps_text(fb,x+12,y+62,13,line,5);
    snprintf(line,sizeof(line),"读取 %s · 写入 %s",sd_info.read_verified?"已通过":"未验证",sd_info.write_verified?"已通过":"未验证");ps_text(fb,x+12,y+92,13,line,5);
    if(!v.landscape){ps_wrap(fb,18,422,v.w-36,13,2,sd_message[0]?sd_message:ps_storage_target(),5);if(sd_view==SD_OVERVIEW)ps_button(fb,18,476,v.w-36,38,"笔记与划线…",false);}
    else {ps_wrap(fb,18,248,280,12,1,ps_storage_target(),5);if(sd_view==SD_OVERVIEW)ps_button(fb,316,238,v.w-332,32,"笔记与划线…",false);}
}
static void sd_render_records(uint8_t *fb){
    ps_view_t v=ps_view();bool progress=sd_view==SD_IMPORT_PROGRESS,result=sd_view==SD_IMPORT_RESULT;
    const char *title=progress?"正在合并笔记":result?(sd_import_result.status==PS_READER_IMPORT_OK?"笔记合并完成":sd_import_result.status==PS_READER_IMPORT_NO_SOURCE?"无需合并笔记":"笔记尚未合并"):sd_records_available?"合并笔记与划线":"笔记与划线存储";
    ps_text(fb,18,v.landscape?86:94,20,title,2);
    const char *message=progress?"正在读取并校验记录，请保持开机，不要拔出TF卡。":result?sd_message:
        sd_records_available?(sd_records_current_is_sd?"从内置存储合并到当前TF卡。相同记录只保留一份，源库和图书文件保留。":"从TF卡合并到当前内置存储。相同记录只保留一份，源库和图书文件保留。"):
        sd_records_current_available?(sd_records_current_is_sd?"笔记保存在TF卡。内置存储暂不可读取，没有其他笔记库可合并。已有TF卡笔记可继续使用。":"笔记保存在内置存储。TF卡笔记库暂不可读取，挂载TF卡后可检查并合并记录。"):
        "笔记存储暂不可用。请先返回检查存储状态；已有文件保留，未执行合并。";
    ps_wrap(fb,18,v.landscape?124:144,v.w-36,15,v.landscape?3:6,message,3);
    if(!progress){char limit[112];snprintf(limit,sizeof(limit),"最多 %u 条记录 · 单条 %u UTF-8字节",PS_READER_RECORD_CAPACITY,PS_READER_NOTE_MAX_BYTES);ps_wrap(fb,18,v.landscape?218:358,v.w-36,13,v.landscape?2:3,limit,5);}
}
static void sd_render_confirm(uint8_t *fb){
    ps_view_t v=ps_view();
    ps_text(fb,18,v.landscape?86:94,20,sd_view==SD_BACKUP?"请先备份卡内文件":"确认清空这张 TF 卡？",2);
    ps_wrap(fb,18,v.landscape?122:140,v.w-36,14,v.landscape?2:4,
        sd_view==SD_BACKUP?"格式化会删除 TF 卡内的图书、字体与笔记。请先在电脑备份，再继续。":"此操作无法撤销。只清空下方这张 TF 卡，完成后会重新挂载并验证读写。",3);
    int top=v.landscape?188:276;ps_box(fb,18,top,v.w-36,v.landscape?54:94,6,10,false);
    char line[120];snprintf(line,sizeof(line),"当前卡：%s",sd_confirm.name[0]?sd_confirm.name:"TF 卡");ps_text(fb,30,top+12,16,line,2);
    sd_capacity(line,sizeof(line),&sd_confirm);ps_wrap(fb,v.landscape?280:30,top+(v.landscape?14:48),v.landscape?v.w-310:v.w-60,12,v.landscape?1:2,line,5);
    ps_wrap(fb,18,v.landscape?248:400,v.w-36,12,v.landscape?1:3,"请保持设备开机，不要拔出卡片。",5);
}
static void sd_render_progress(uint8_t *fb){
    ps_view_t v=ps_view();int x=v.landscape?98:24,y=v.landscape?100:182;
    ps_icon(fb,v.landscape?24:v.w/2-24,v.landscape?100:104,48,PI_REFRESH,2);
    ps_text(fb,x,y,20,sd_action==SD_FORMAT?"正在格式化 TF 卡":"正在检测并验证",2);
    ps_wrap(fb,x,y+42,v.w-x-24,16,2,sd_stage_title(),3);
    ps_wrap(fb,24,v.landscape?214:328,v.w-48,13,v.landscape?2:4,"请保持设备开机，不要拔出 TF 卡。完成后会显示容量与读写验证结果。",5);
}
void ps_storage_render(uint8_t *fb){
    ps_heading(fb,"TF 卡与存储",NULL);
    if(sd_view==SD_RECORDS||sd_view==SD_IMPORT_PROGRESS||sd_view==SD_IMPORT_RESULT)sd_render_records(fb);
    else if(sd_running)sd_render_progress(fb);
    else if(sd_view==SD_BACKUP||sd_view==SD_ERASE)sd_render_confirm(fb);
    else sd_render_status(fb);
    sd_control_t controls[2];int count=sd_controls(controls);for(int i=0;i<count;i++){
        sd_control_t *c=&controls[i];ps_button(fb,c->x,c->y,c->w,c->h,c->label,c->primary);
    }
}
bool ps_storage_event(const ps_event_t *e){
    if(sd_running||sd_view==SD_IMPORT_PROGRESS)return true;
    if(e->type!=PS_TAP)return false;
    ps_view_t v=ps_view();
    if(sd_view==SD_OVERVIEW&&ps_hit(e->x,e->y,v.landscape?316:18,v.landscape?238:476,v.landscape?v.w-332:v.w-36,v.landscape?32:38)){sd_records_open();return true;}
    sd_control_t controls[2];int count=sd_controls(controls),id=-1;
    for(int i=0;i<count;i++)if(ps_hit(e->x,e->y,controls[i].x,controls[i].y,controls[i].w,controls[i].h)){id=controls[i].id;break;}
    if(id<0)return false;
    if(id==SD_IMPORT){if(!sd_records_ready()){ps_toast("存储状态已改变，请返回后重新检查");return true;}sd_view=SD_IMPORT_PROGRESS;return true;}
    if(id==SD_CANCEL||id==SD_DONE){sd_view=SD_OVERVIEW;return true;}
    if(id==SD_CONTINUE){sd_update();if(!sd_same_card(&sd_confirm,&sd_info)||!sd_can_format())sd_view=SD_OVERVIEW;else{sd_view=SD_ERASE;sd_confirm_at=ps_now();}return true;}
    if(id==SD_CONFIRM){
        if(ps_now()-sd_confirm_at<500)return true;
        sd_update();if(!sd_same_card(&sd_confirm,&sd_info)||!sd_can_format()){sd_view=SD_OVERVIEW;ps_toast("卡片已改变或不可用，请重新确认");}
        else sd_start(SD_FORMAT);
        return true;
    }
    if(id==SD_OPEN_FORMAT){sd_update();if(sd_can_format()){sd_confirm=sd_info;sd_view=SD_BACKUP;}return true;}
    if(id==SD_DETECT){sd_update();if(sd_info.state==READ_PICO_SD_STATE_EXPORTED)ps_toast("请先结束 USB 传输");else sd_start(SD_CHECK);return true;}
    return false;
}
