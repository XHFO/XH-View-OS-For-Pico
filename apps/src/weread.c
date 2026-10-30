/*
 * SPDX-FileCopyrightText: 2026 XH Lab
 * SPDX-License-Identifier: Apache-2.0
 * 中文：TF卡微信读书书架与个人笔记；复用设备本地扫码配置与同步服务，离线分页及独立阅读位置。
 * English: TF WeRead shelf and personal notes, using the device-local phone setup/sync service with offline pages and separate progress.
 * 冻结：API Key只由设备服务保存；模块不接触凭据、不下载正文、不修改同步快照；绘制无IO，移除包保留数据。
 * Frozen: The device service alone stores API Keys; no credentials, full-book downloads or snapshot writes in this module; render has no IO and uninstall preserves data.
 */
#include "pico_app_sdk.h"
#include "../include/weread_snapshot.h"

enum { SHELF, NOTES, CONTENT_ERROR, CONNECTION, DISCOVER, ONLINE };
enum { SNAPSHOT_OK, SNAPSHOT_MISSING, SNAPSHOT_BAD };
enum { FONT=20, LINE_HEIGHT=29, BODY_TOP=108, FOOTER=70 };
static const pico_app_host_t *host;
static uint8_t index_data[WRS_INDEX_BYTES],chunk_data[WRS_CHUNK_BYTES],position[WRS_POSITION_BYTES];
static unsigned mode,snapshot_state,book_slot,chunk_number,text_bytes,page_offset,shelf_page;
static bool position_valid,position_dirty,touching,save_failed;
static int64_t save_due;
static pico_app_weread_status_t connection;
static bool service_available;
static uint32_t loaded_revision;
static int64_t service_due;
static bool wre_turn(bool forward),wre_back(void);

static bool service(uint32_t command){
    if(!service_available)return false;
    connection.bytes=sizeof(connection);
    return host->weread(command,&connection);
}
static const char *connection_error(void){
    const char *code=connection.error;
    if(!host->compare(code,"offline",8))return "请先连接 Wi-Fi，再重新同步。";
    if(!host->compare(code,"time",5))return "请先在日期与时间设置中校准时间。";
    if(!host->compare(code,"auth",5))return "API Key 已失效，请扫码重新填写。";
    if(!host->compare(code,"storage",8))return "请确认 TF 卡可读写，且未连接电脑传输。";
    if(!host->compare(code,"too_large",10))return "同步内容超过设备容量，请在手机上减少书籍。";
    if(!host->compare(code,"cancelled",10))return "同步已取消，原有离线内容仍保留。";
    if(!host->compare(code,"upgrade",8))return "请升级固件后再同步。";
    if(!host->compare(code,"selection",10))return "请扫码打开手机配置，选择要同步的书籍。";
    return "同步未完成，请检查网络后重试。";
}

static uint32_t u32(const uint8_t *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void put32(uint8_t *p,uint32_t n){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(n>>(8*i));}
static bool zero(const uint8_t *p,unsigned bytes){for(unsigned i=0;i<bytes;i++)if(p[i])return false;return true;}
static const uint8_t *book(unsigned slot){return index_data+WRS_HEADER_BYTES+slot*WRS_BOOK_BYTES;}
static unsigned books(void){return snapshot_state==SNAPSHOT_OK?u32(index_data+WRS_I_BOOKS):0;}
static unsigned chunks(unsigned slot){return u32(book(slot)+WRS_B_CHUNKS);}
static unsigned rows(void){int count=(host->view().h-BODY_TOP-FOOTER)/LINE_HEIGHT;return count>0?(unsigned)count:1;}
static unsigned shelf_rows(void){int count=(host->view().h-108-76)/72;return count>0?(unsigned)count:1;}
static unsigned shelf_pages(void){unsigned n=books(),r=shelf_rows();return n?(n+r-1)/r:1;}

// 严格解码Unicode标量；拒绝过长编码、代理项、控制符与越界。
// Decode strict Unicode scalars, rejecting overlong encodings, surrogates, controls and out-of-bounds input.
static unsigned scalar(const uint8_t *p,unsigned left,uint32_t *out){
    if(!left)return 0;
    uint32_t c=p[0];unsigned n=c<0x80?1:c>=0xc2&&c<=0xdf?2:c>=0xe0&&c<=0xef?3:c>=0xf0&&c<=0xf4?4:0;
    if(!n||left<n)return 0;
    if(n>1){c&=(1u<<(7-n))-1u;for(unsigned i=1;i<n;i++){if((p[i]&0xc0)!=0x80)return 0;c=(c<<6)|(p[i]&63u);}}
    if((n==2&&c<0x80)||(n==3&&c<0x800)||(n==4&&c<0x10000)||c>0x10ffff||(c>=0xd800&&c<=0xdfff))return 0;
    *out=c;return n;
}
static bool utf8(const uint8_t *p,unsigned bytes,bool multiline){
    for(unsigned at=0;at<bytes;){uint32_t c;unsigned n=scalar(p+at,bytes-at,&c);if(!n||((c<32||(c>=127&&c<=159))&&!(multiline&&(c==9||c==10))))return false;at+=n;}
    return true;
}
static bool string(const uint8_t *p,unsigned bytes,bool nonempty,bool identifier){
    unsigned n=0;while(n<bytes&&p[n])n++;
    if(n==bytes||(nonempty&&!n)||!zero(p+n,bytes-n)||!utf8(p,n,false))return false;
    if(identifier)for(unsigned i=0;i<n;i++)if(!((p[i]>='a'&&p[i]<='z')||(p[i]>='A'&&p[i]<='Z')||(p[i]>='0'&&p[i]<='9')||p[i]=='_'||p[i]=='-'))return false;
    return true;
}
static bool valid_index(void){
    if(host->compare(index_data,WRS_INDEX_MAGIC,8)||u32(index_data+WRS_I_SCHEMA)!=WRS_SCHEMA||u32(index_data+WRS_I_BYTES)!=WRS_INDEX_BYTES||
       !u32(index_data+WRS_I_GENERATION)||u32(index_data+WRS_I_BOOKS)>WRS_BOOK_MAX||!zero(index_data+WRS_I_RESERVED,28))return false;
    unsigned n=u32(index_data+WRS_I_BOOKS);uint32_t total=0;
    for(unsigned i=0;i<n;i++){
        const uint8_t *b=book(i);uint32_t notes=u32(b+WRS_B_NOTES),count=u32(b+WRS_B_CHUNKS);
        if(!string(b,64,true,true)||!string(b+WRS_B_TITLE,128,true,false)||!string(b+WRS_B_AUTHOR,48,false,false)||
           !zero(b+WRS_B_RESERVED,8)||count>WRS_CHUNK_MAX||(!notes)!=(!count)||notes>UINT32_MAX-total)return false;
        total+=notes;
        for(unsigned j=0;j<i;j++)if(!host->compare(b,book(j),64))return false;
    }
    return total==u32(index_data+WRS_I_NOTES)&&zero(book(n),(WRS_BOOK_MAX-n)*WRS_BOOK_BYTES);
}
static bool valid_position(void){
    unsigned slot=u32(position+WRS_P_BOOK),count=u32(index_data+WRS_I_BOOKS);
    if(host->compare(position,WRS_POSITION_MAGIC,8)||u32(position+WRS_P_SCHEMA)!=WRS_SCHEMA||u32(position+WRS_P_BYTES)!=WRS_POSITION_BYTES||
       u32(position+WRS_P_GENERATION)!=u32(index_data+WRS_I_GENERATION)||slot>=count||u32(position+WRS_P_MODE)>1||
       !zero(position+WRS_P_RESERVED,4)||!zero(position+WRS_P_TAIL,24)||host->compare(position+WRS_P_ID,book(slot),64))return false;
    unsigned count_chunks=chunks(slot),part=u32(position+WRS_P_CHUNK),offset=u32(position+WRS_P_OFFSET);
    return count_chunks?(part<count_chunks&&offset<WRS_TEXT_BYTES):!part&&!offset;
}
static void load_snapshot(void){
    bool loaded=host->load("index",index_data,sizeof(index_data));
    snapshot_state=!loaded?SNAPSHOT_MISSING:valid_index()?SNAPSHOT_OK:SNAPSHOT_BAD;
    mode=SHELF;book_slot=chunk_number=text_bytes=page_offset=shelf_page=0;
    position_valid=false;position_dirty=save_failed=false;
    if(snapshot_state==SNAPSHOT_OK&&host->load("position",position,sizeof(position))&&valid_position()){
        position_valid=true;book_slot=u32(position+WRS_P_BOOK);shelf_page=book_slot/shelf_rows();
    }
}
static void remember(void){
    if(snapshot_state!=SNAPSHOT_OK||book_slot>=books()||mode==CONTENT_ERROR)return;
    host->set(position,0,sizeof(position));host->copy(position,WRS_POSITION_MAGIC,8);
    put32(position+WRS_P_SCHEMA,WRS_SCHEMA);put32(position+WRS_P_BYTES,WRS_POSITION_BYTES);
    put32(position+WRS_P_GENERATION,u32(index_data+WRS_I_GENERATION));put32(position+WRS_P_BOOK,book_slot);
    put32(position+WRS_P_CHUNK,chunk_number);put32(position+WRS_P_OFFSET,page_offset);put32(position+WRS_P_MODE,mode==NOTES);
    host->copy(position+WRS_P_ID,book(book_slot),64);position_valid=position_dirty=true;save_failed=false;save_due=host->time_us()+1500000;
}
static bool save(void){
    if(!position_dirty)return true;
    if(!host->store("position",position,sizeof(position))){save_failed=true;return false;}
    position_dirty=save_failed=false;return true;
}
static bool valid_chunk(unsigned slot,unsigned part){
    return !host->compare(chunk_data,WRS_CHUNK_MAGIC,8)&&u32(chunk_data+WRS_C_SCHEMA)==WRS_SCHEMA&&u32(chunk_data+WRS_C_BYTES)==WRS_CHUNK_BYTES&&
        u32(chunk_data+WRS_C_GENERATION)==u32(index_data+WRS_I_GENERATION)&&u32(chunk_data+WRS_C_BOOK)==slot&&u32(chunk_data+WRS_C_INDEX)==part&&
        u32(chunk_data+WRS_C_COUNT)==chunks(slot)&&u32(chunk_data+WRS_C_TEXT)>0&&u32(chunk_data+WRS_C_TEXT)<=WRS_TEXT_BYTES&&
        zero(chunk_data+WRS_C_RESERVED,28)&&utf8(chunk_data+WRS_HEADER_BYTES,u32(chunk_data+WRS_C_TEXT),true)&&
        zero(chunk_data+WRS_HEADER_BYTES+u32(chunk_data+WRS_C_TEXT),WRS_TEXT_BYTES-u32(chunk_data+WRS_C_TEXT));
}
static bool load_chunk(unsigned slot,unsigned part){
    char key[32];host->format(key,sizeof(key),"g%08x-b%02x-c%03x",u32(index_data+WRS_I_GENERATION),slot,part);
    book_slot=slot;chunk_number=part;page_offset=text_bytes=0;
    if(!host->load(key,chunk_data,sizeof(chunk_data))||!valid_chunk(slot,part)){mode=CONTENT_ERROR;return false;}
    text_bytes=u32(chunk_data+WRS_C_TEXT);mode=NOTES;return true;
}
static bool open_book(unsigned slot){
    unsigned part=0,offset=0;
    if(position_valid&&u32(position+WRS_P_BOOK)==slot){part=u32(position+WRS_P_CHUNK);offset=u32(position+WRS_P_OFFSET);}
    book_slot=slot;chunk_number=page_offset=text_bytes=0;
    if(!chunks(slot)){mode=NOTES;remember();return true;}
    if(!load_chunk(slot,part))return true;
    if(offset<text_bytes&&(offset==0||(chunk_data[WRS_HEADER_BYTES+offset]&0xc0)!=0x80))page_offset=offset;
    remember();return true;
}

// 使用保守字宽在模块内分页；每行完整UTF-8且不会依赖宿主wrap的截断行为。
// Paginate using conservative advances; every line is complete UTF-8 without relying on host wrap truncation.
static unsigned advance(uint32_t c){return c==9?40:c<128&&c!='W'&&c!='M'&&c!='@'?13:c>0xffff?40:20;}
static unsigned next_line(unsigned at,char *out,unsigned capacity){
    const uint8_t *text=chunk_data+WRS_HEADER_BYTES;unsigned start=at,n=0,width=0,limit=(unsigned)(host->view().w-40);
    while(at<text_bytes){
        uint32_t c;unsigned count=scalar(text+at,text_bytes-at,&c);if(!count)break;
        if(c==10){at+=count;break;}
        unsigned pixels=advance(c);
        if(n&&(width+pixels>limit||n+count>=capacity))break;
        if(c==9){if(out)out[n]=' ';n++;}else{if(out)host->copy(out+n,text+at,count);n+=count;}
        width+=pixels;at+=count;
    }
    if(out)out[n]=0;
    return at>start?at:start+1;
}
static unsigned next_page(unsigned at){for(unsigned i=0;i<rows()&&at<text_bytes;i++)at=next_line(at,NULL,256);return at;}
static unsigned last_page(void){unsigned at=0;while(at<text_bytes){unsigned next=next_page(at);if(next>=text_bytes)return at;at=next;}return 0;}
static unsigned previous_page(unsigned current){unsigned at=0,previous=0;while(at<current){unsigned next=next_page(at);if(next>=current)return at;previous=at;at=next;}return previous;}
static bool turn(bool forward){
    if(mode==ONLINE)return wre_turn(forward);
    if(mode==SHELF){unsigned count=shelf_pages();if(forward&&shelf_page+1<count){shelf_page++;return true;}if(!forward&&shelf_page){shelf_page--;return true;}return false;}
    if(mode!=NOTES||!text_bytes)return false;
    if(forward){
        unsigned next=next_page(page_offset);
        if(next<text_bytes)page_offset=next;
        else if(chunk_number+1<chunks(book_slot)){if(!load_chunk(book_slot,chunk_number+1))return true;}
        else return false;
    }else if(page_offset)page_offset=previous_page(page_offset);
    else if(chunk_number){if(!load_chunk(book_slot,chunk_number-1))return true;page_offset=last_page();}
    else return false;
    remember();return true;
}
static bool back(void){
    if(mode==DISCOVER||mode==ONLINE)return wre_back();
    if(mode!=SHELF){bool was_notes=mode==NOTES;mode=SHELF;shelf_page=book_slot/shelf_rows();if(was_notes)remember();return true;}
    host->navigate(PGA_PAGE_LAUNCHER);return true;
}
static void clipped(uint8_t *fb,int x,int y,int size,const uint8_t *source,unsigned bytes,int width,int gray){
    char line[256];unsigned at=0,n=0,pixels=0;bool shortened=false;
    while(at<bytes&&source[at]){uint32_t c;unsigned length=scalar(source+at,bytes-at,&c);if(!length)break;unsigned next=advance(c),tail=at+length<bytes&&source[at+length]?20:0;if(pixels+next+tail>(unsigned)width||n+length+4>=sizeof(line)){shortened=true;break;}host->copy(line+n,source+at,length);n+=length;at+=length;pixels+=next;}
    if(shortened&&n+3<sizeof(line)){host->copy(line+n,"…",3);n+=3;}line[n]=0;host->text(fb,x,y,size,line,gray);
}
static void header(uint8_t *fb,const char *title){
    pico_app_view_t v=host->view();host->fill(fb,0,0,v.w,v.h,15);host->button(fb,16,20,58,32,"返回",false);
    clipped(fb,88,25,20,(const uint8_t*)title,128,v.w-104,0);host->line(fb,16,67,v.w-16,67,10);
}
static void footer(uint8_t *fb,unsigned current,unsigned count,unsigned part,unsigned parts){
    pico_app_view_t v=host->view();int y=v.h-52;char label[64];
    host->button(fb,16,y,78,34,"上一页",false);host->button(fb,v.w-94,y,78,34,"下一页",false);
    if(parts>1)host->format(label,sizeof(label),"%u/%u · %u/%u",current,count,part,parts);else host->format(label,sizeof(label),"%u / %u",current,count);
    host->center(fb,v.w/2,y+10,12,label,3);
}
#include "../include/weread_explore.h"

static void render_connection(uint8_t *fb){
    pico_app_view_t v=host->view();char label[160];
    const char *state=!service_available?"需要支持设备同步的新版固件":connection.flags&PGA_WEREAD_CONFIGURED?"API Key 已保存到此设备":"首次连接 · 在手机上填写 API Key";
    host->text(fb,20,78,12,state,4);
    host->text(fb,20,114,18,"手机配置，Pico 独立同步",0);
    host->text(fb,20,151,14,"1  手机与 Pico 连接同一个 Wi-Fi",3);
    host->text(fb,20,178,14,"2  扫描二维码，打开设备配置后台",3);
    host->text(fb,20,205,14,"3  填写官方 API Key，选择书籍同步",3);
    bool configured=connection.flags&PGA_WEREAD_CONFIGURED;
    int width=configured?(v.w-52)/2:v.w-40,y=v.landscape?246:270;
    host->button(fb,20,y,width,40,"手机扫码设置",false);
    if(configured)host->button(fb,32+width,y,width,40,connection.flags&PGA_WEREAD_BUSY?(connection.state==PGA_WEREAD_QUERYING?"正在查询":"正在同步"):"立即同步",false);
    if(!service_available)host->format(label,sizeof(label),"旧版固件仍可查看已有的离线书架与笔记。");
    else if(connection.flags&PGA_WEREAD_BUSY){
        if(connection.state==PGA_WEREAD_QUERYING)host->format(label,sizeof(label),"正在处理在线内容…");
        else if(connection.total)host->format(label,sizeof(label),"%s · %u / %u",connection.state==PGA_WEREAD_SHELF?"正在获取书架":"正在同步笔记",connection.progress,connection.total);
        else host->format(label,sizeof(label),"%s",connection.state==PGA_WEREAD_SHELF?"正在获取书架…":"正在同步笔记…");
    }else if(connection.state==PGA_WEREAD_ERROR)host->format(label,sizeof(label),"%s",connection_error());
    else if(connection.state==PGA_WEREAD_DONE)host->format(label,sizeof(label),"同步完成 · %u 本书 · %u 条笔记",connection.books,connection.notes);
    else host->format(label,sizeof(label),"只同步书架、个人划线与想法，不含书籍正文。");
    host->wrap(fb,20,y+54,v.w-40,12,v.landscape?1:3,label,4);
}
static void render(uint8_t *fb){
    if(mode==DISCOVER){wre_render_discover(fb);return;}if(mode==ONLINE){wre_render_online(fb);return;}
    pico_app_view_t v=host->view();header(fb,mode==SHELF?"微信读书":mode==CONNECTION?"连接与同步":(const char*)book(book_slot)+WRS_B_TITLE);
    if(mode==SHELF)host->button(fb,v.w-80,20,64,32,"发现",false);
    else if(mode==NOTES)host->button(fb,v.w-80,20,64,32,"详情",false);
    if(mode==CONNECTION){render_connection(fb);return;}
    char label[128];
    if(mode==SHELF){
        host->text(fb,16,78,12,"书架与笔记",3);host->button(fb,v.w-192,76,112,28,"连接与同步",false);host->button(fb,v.w-72,76,56,28,"刷新",false);
        if(snapshot_state!=SNAPSHOT_OK||!books()){
            host->text(fb,20,145,20,snapshot_state==SNAPSHOT_BAD?"同步内容不可读取":"还没有同步书架",0);
            host->wrap(fb,20,194,v.w-40,16,5,snapshot_state==SNAPSHOT_BAD?"原文件已保留。请打开「连接与同步」，\n在此设备重新同步书架与笔记。":"打开「连接与同步」，手机扫码填写\nAPI Key。Pico 会独立同步书架、\n个人划线与想法，不含书籍正文。",4);return;
        }
        unsigned per_page=shelf_rows(),page=shelf_page;if(page>=shelf_pages())page=shelf_pages()-1;
        for(unsigned row=0;row<per_page;row++){
            unsigned slot=page*per_page+row;if(slot>=books())break;const uint8_t *b=book(slot);int y=112+(int)row*72;
            bool recent=position_valid&&u32(position+WRS_P_BOOK)==slot;host->box(fb,16,y,v.w-32,64,6,recent?13:15,true);host->box(fb,16,y,v.w-32,64,6,recent?7:11,false);
            clipped(fb,28,y+10,18,b+WRS_B_TITLE,128,v.w-56,0);
            host->format(label,sizeof(label),"%s · %u 条笔记",b[WRS_B_AUTHOR]?(const char*)b+WRS_B_AUTHOR:"未标注作者",u32(b+WRS_B_NOTES));
            clipped(fb,28,y+37,12,(const uint8_t*)label,sizeof(label),v.w-56,4);
        }
        footer(fb,page+1,shelf_pages(),0,0);return;
    }
    if(mode==CONTENT_ERROR){
        host->text(fb,20,112,20,"这段笔记暂时无法读取",0);host->wrap(fb,20,157,v.w-40,16,4,"文件缺失、损坏或尚未完成同步。\n原始内容和阅读位置均未覆盖。",4);
        host->button(fb,20,v.h-112,100,38,"重试读取",false);return;
    }
    host->format(label,sizeof(label),"划线与想法 · %u 条",u32(book(book_slot)+WRS_B_NOTES));host->text(fb,20,78,12,label,4);
    if(!text_bytes){host->text(fb,20,145,20,"这本书还没有笔记",0);host->wrap(fb,20,194,v.w-40,16,3,"保存新的划线或想法后，返回书架，\n打开「连接与同步」即可更新。",4);return;}
    unsigned at=page_offset;
    for(unsigned row=0;row<rows()&&at<text_bytes;row++){char line[256];at=next_line(at,line,sizeof(line));host->text(fb,20,BODY_TOP+(int)row*LINE_HEIGHT,FONT,line,0);}
    unsigned count=0,current=1;at=0;
    while(at<text_bytes){if(at<page_offset)current++;count++;at=next_page(at);}
    if(current>count)current=count;
    footer(fb,current,count,chunk_number+1,chunks(book_slot));
}
static bool event(const pico_app_event_t *e){
    if(!e)return false;
    if(mode==DISCOVER||mode==ONLINE){if(e->type==PGA_PRESS){touching=true;return false;}if(e->type==PGA_RELEASE||e->type==PGA_CANCEL){touching=false;return false;}touching=false;return wre_event(e);}
    if(e->type==PGA_PRESS){touching=true;return false;}
    if(e->type==PGA_RELEASE||e->type==PGA_CANCEL){touching=false;if(position_dirty)save_due=host->time_us()+1500000;return false;}
    if(e->type==PGA_SWIPE_L||e->type==PGA_SWIPE_U){touching=false;return turn(true);}
    if(e->type==PGA_SWIPE_R||e->type==PGA_SWIPE_D){touching=false;return turn(false);}
    if(e->type!=PGA_TAP)return false;
    touching=false;
    pico_app_view_t v=host->view();if(e->x<0||e->y<0||e->x>=v.w||e->y>=v.h)return false;
    if(e->x>=16&&e->x<74&&e->y>=20&&e->y<52)return back();
    if((mode==SHELF||mode==NOTES)&&e->x>=v.w-80&&e->x<v.w-16&&e->y>=20&&e->y<52){if(mode==NOTES)return wre_open_info((const char*)book(book_slot));mode=DISCOVER;return true;}
    if(mode==CONNECTION){
        bool configured=connection.flags&PGA_WEREAD_CONFIGURED;
        int width=configured?(v.w-52)/2:v.w-40,y=v.landscape?246:270;
        if(e->y<y||e->y>=y+40)return false;
        bool setup=e->x>=20&&e->x<20+width,sync=configured&&e->x>=32+width&&e->x<32+2*width;
        if(!setup&&!sync)return false;
        if(!service_available){host->toast("请升级固件后使用手机扫码同步");return true;}
        if(connection.flags&PGA_WEREAD_BUSY){host->toast("正在同步，请稍候");return true;}
        if(!save()){host->toast("阅读位置尚未保存，请检查 TF 卡");return true;}
        if(setup||!(connection.flags&PGA_WEREAD_CONFIGURED)){
            if(!service(PGA_WEREAD_OPEN_SETUP))host->toast("暂时无法打开配置，请稍后重试");
        }else if(!service(PGA_WEREAD_SYNC))host->toast(connection_error());
        service_due=host->time_us()+500000;return true;
    }
    if(mode==CONTENT_ERROR&&e->x>=20&&e->x<120&&e->y>=v.h-112&&e->y<v.h-74){if(load_chunk(book_slot,chunk_number))remember();return true;}
    if(mode==SHELF&&e->x>=v.w-192&&e->x<v.w-80&&e->y>=76&&e->y<104){mode=CONNECTION;(void)service(PGA_WEREAD_STATUS);return true;}
    if(mode==SHELF&&e->x>=v.w-72&&e->x<v.w-16&&e->y>=76&&e->y<104){
        if(connection.flags&PGA_WEREAD_BUSY){host->toast("同步完成后会更新书架");return true;}
        if(!save()){host->toast("阅读位置尚未保存，请检查 TF 卡");return true;}load_snapshot();loaded_revision=connection.revision;return true;
    }
    if(e->y>=v.h-52&&e->y<v.h-18){if(e->x>=16&&e->x<94)return turn(false);if(e->x>=v.w-94&&e->x<v.w-16)return turn(true);}
    if(mode==SHELF&&snapshot_state==SNAPSHOT_OK&&e->x>=16&&e->x<v.w-16&&e->y>=112){
        unsigned row=(unsigned)(e->y-112)/72,slot=shelf_page*shelf_rows()+row;
        if(row<shelf_rows()&&(e->y-112)%72<64&&slot<books())return open_book(slot);
    }
    return false;
}
static bool key(int value){return value==-1?back():value==0?turn(false):value==2?turn(true):false;}
static bool tick(void){
    if(position_dirty&&!touching&&!save_failed&&host->time_us()>=save_due)(void)save();
    if(!service_available||touching||host->time_us()<service_due)return false;
    service_due=host->time_us()+500000;
    if(wre_tick())return true;
    pico_app_weread_status_t before;host->copy(&before,&connection,sizeof(before));(void)service(PGA_WEREAD_STATUS);
    bool changed=host->compare(&before,&connection,sizeof(connection))!=0;
    // 同步在后台原子提交；阅读中保留当前内容，回到书架/连接页才采纳新快照。
    // Sync commits atomically in the background; keep the current notes until returning to the shelf/setup page.
    if(!(connection.flags&PGA_WEREAD_BUSY)&&connection.state==PGA_WEREAD_DONE&&connection.revision!=loaded_revision&&(mode==SHELF||mode==CONNECTION)&&save()){
        bool was_connection=mode==CONNECTION;load_snapshot();if(was_connection)mode=CONNECTION;
        loaded_revision=connection.revision;return true;
    }
    return changed&&mode==CONNECTION;
}
static void action(int value){(void)value;}
static uint32_t status(void){return position_dirty?PGA_STATUS_UNSAVED:0;}
static void stop(void){touching=false;if(wre_input_wait&&host->bytes>=offsetof(pico_app_host_t,weread_input)+sizeof(host->weread_input)&&host->weread_input)(void)host->weread_input(PGA_WEREAD_INPUT_CANCEL,wre_keyword,sizeof(wre_keyword));wre_input_wait=false;}
static bool start(const pico_app_host_t *api){
    if(!api||api->abi!=PICO_APP_ABI||api->bytes<PICO_APP_HOST_V1_BYTES||!api->view||!api->navigate||!api->load||!api->store||!api->time_us||!api->toast||
       !api->fill||!api->line||!api->box||!api->text||!api->center||!api->wrap||!api->button||!api->format||!api->copy||!api->set||!api->compare)return false;
    host=api;touching=false;service_available=api->bytes>=offsetof(pico_app_host_t,weread)+sizeof(api->weread)&&api->weread;
    host->set(&connection,0,sizeof(connection));connection.bytes=sizeof(connection);(void)service(PGA_WEREAD_STATUS);
    loaded_revision=connection.revision;service_due=host->time_us()+500000;load_snapshot();wre_init();return true;
}
static const pico_app_exports_t exports={.abi=PICO_APP_ABI,.bytes=sizeof(pico_app_exports_t),.version=WRS_APP_VERSION,.kind=WRS_APP_KIND,.id="weread",
    .start=start,.render=render,.event=event,.key=key,.tick=tick,.action=action,.status=status,.save=save,.stop=stop,.ink=NULL};
__attribute__((visibility("default"))) const pico_app_exports_t *pico_app_query(void){return &exports;}
