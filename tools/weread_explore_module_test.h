/* SPDX-License-Identifier: Apache-2.0
 * 中文：TF在线应用合成官方响应专项；不访问网络或设备。
 * English: TF exploration with synthetic official responses, without network/hardware.
 */
static pico_app_weread_query_status_t explore_status;
static pico_app_weread_query_t explore_last_request;
static char explore_raw[600000],explore_input_value[160];
static unsigned explore_raw_bytes,explore_kind_counts[10],explore_input_opens,explore_link_opens,explore_cover_draws;
static bool explore_hold,explore_fail_read,explore_submit_fail,explore_input_ready;
static char explore_opened_link[1024];
static unsigned explore_busy_rejects;
static size_t explore_read_limit;
static void explore_response(unsigned kind){
    unsigned used=0;explore_raw[0]=0;
    if(kind==PGA_WEREAD_SEARCH){
        used+=(unsigned)snprintf(explore_raw+used,sizeof(explore_raw)-used,"{\"hasMore\":1,\"results\":[{\"scope\":17,\"title\":\"电子书\",\"books\":[");
        for(unsigned i=0;i<12;i++)used+=(unsigned)snprintf(explore_raw+used,sizeof(explore_raw)-used,"%s{\"searchIdx\":%u,\"bookInfo\":{\"bookId\":\"b%u\",\"title\":\"搜索书%02u\",\"author\":\"测试作者\",\"newRating\":96}}",i?",":"",1001+i,i,i);
        used+=(unsigned)snprintf(explore_raw+used,sizeof(explore_raw)-used,"]}]}");
    }else if(kind==PGA_WEREAD_INFO){
        bool other=!strcmp(explore_last_request.book_id,"similar-id");
        used+=(unsigned)snprintf(explore_raw+used,sizeof(explore_raw)-used,"{\"bookId\":\"%s\",\"title\":\"%s\",\"author\":\"%s\",\"readerLink\":\"https://weread.qq.com/web/reader/%s\",\"cover\":\"https://cdn.weread.qq.com/fixture-cover.jpg\",\"intro\":\"",other?"similar-id":"b0",other?"另一部图书":"三体｜中文版",other?"另一位作者":"刘慈欣",other?"fixture-b":"fixture");
        for(unsigned i=0;i<180;i++)used+=(unsigned)snprintf(explore_raw+used,sizeof(explore_raw)-used,"简介%03u：完整段落，保留字符\\uD83D\\uDE00。\\n",i);
        used+=(unsigned)snprintf(explore_raw+used,sizeof(explore_raw)-used,"简介结束\"}");
    }else if(kind==PGA_WEREAD_CHAPTERS){
        used+=(unsigned)snprintf(explore_raw+used,sizeof(explore_raw)-used,"{\"chapters\":[");
        for(unsigned i=0;i<2000;i++)used+=(unsigned)snprintf(explore_raw+used,sizeof(explore_raw)-used,"%s{\"chapterUid\":%u,\"title\":\"章节%04u\",\"wordCount\":2048,\"level\":1,\"price\":%u,\"paid\":%u}",i?",":"",i+1,i,i%2,i%3==0);
        used+=(unsigned)snprintf(explore_raw+used,sizeof(explore_raw)-used,"]}");
    }else if(kind==PGA_WEREAD_PROGRESS)used=(unsigned)snprintf(explore_raw,sizeof(explore_raw),"{\"book\":{\"progress\":1,\"recordReadingTime\":3661}}");
    else if(kind==PGA_WEREAD_RECOMMEND)used=(unsigned)snprintf(explore_raw,sizeof(explore_raw),"{\"books\":[{\"bookId\":\"recommend-id\",\"title\":\"推荐图书\",\"author\":\"推荐作者\",\"searchIdx\":77}]}");
    else if(kind==PGA_WEREAD_SIMILAR)used=(unsigned)snprintf(explore_raw,sizeof(explore_raw),"{\"booksimilar\":{\"sessionId\":\"fixture-session\",\"books\":[{\"idx\":88,\"book\":{\"bookInfo\":{\"bookId\":\"similar-id\",\"title\":\"相似图书\",\"author\":\"相似作者\"}}}]}}");
    else if(kind==PGA_WEREAD_STATS)used=(unsigned)snprintf(explore_raw,sizeof(explore_raw),"{\"totalReadTime\":7260,\"readDays\":7,\"dayAverageReadTime\":1200,\"preferCategoryWord\":\"偏好阅读文学\",\"preferTimeWord\":\"偏好夜间阅读\",\"readStat\":[{\"stat\":\"读过\",\"counts\":\"12本\"},{\"stat\":\"笔记\",\"counts\":\"120条\"}],\"readLongest\":[{\"book\":{\"title\":\"阅读排行\"},\"readTime\":6000}]}");
    else if(kind==PGA_WEREAD_HIGHLIGHTS){
        used+=(unsigned)snprintf(explore_raw+used,sizeof(explore_raw)-used,"{\"totalCount\":100,\"items\":[{\"totalCount\":28,\"chapterUid\":1,\"markText\":\"");
        for(unsigned i=0;i<120;i++)used+=(unsigned)snprintf(explore_raw+used,sizeof(explore_raw)-used,"划线%03u：逐字保留。\\n",i);
        used+=(unsigned)snprintf(explore_raw+used,sizeof(explore_raw)-used,"划线结束\"}]}");
    }else if(kind==PGA_WEREAD_REVIEWS){
        used+=(unsigned)snprintf(explore_raw+used,sizeof(explore_raw)-used,"{\"reviewsHasMore\":1,\"synckey\":909,\"reviews\":[{\"idx\":55,\"review\":{\"review\":{\"author\":{\"name\":\"读者甲\"},\"content\":\"");
        for(unsigned i=0;i<220;i++)used+=(unsigned)snprintf(explore_raw+used,sizeof(explore_raw)-used,"书评%03u：完整文字，逐页显示。\\n",i);
        used+=(unsigned)snprintf(explore_raw+used,sizeof(explore_raw)-used,"书评结束\"}}}]}");
    }
    CHECK(used&&used<sizeof(explore_raw));explore_raw_bytes=used;
}
static bool explore_query(const pico_app_weread_query_t *request,pico_app_weread_query_status_t *out){
    CHECK(out&&out->bytes==sizeof(*out));explore_service_calls++;
    if(request){CHECK(request->bytes==sizeof(*request)&&request->kind>=1&&request->kind<=9);if(explore_status.busy){explore_busy_rejects++;memset(out,0,sizeof(*out));out->bytes=sizeof(*out);strcpy(out->error,"busy");return false;}if(explore_submit_fail){memset(out,0,sizeof(*out));out->bytes=sizeof(*out);strcpy(out->error,"offline");return false;}
        memcpy(&explore_last_request,request,sizeof(*request));explore_kind_counts[request->kind]++;explore_response(request->kind);explore_status.request_id++;explore_status.revision++;explore_status.bytes=sizeof(explore_status);explore_status.kind=request->kind;explore_status.busy=true;explore_status.ready=false;explore_status.error[0]=0;
    }else if(!explore_hold&&explore_status.busy){explore_status.busy=false;explore_status.ready=true;explore_status.total_bytes=explore_raw_bytes;explore_status.revision++;}
    *out=explore_status;return true;
}
static bool explore_query_read(uint32_t request_id,uint32_t offset,void *out,size_t capacity,size_t *used){
    CHECK(out&&used&&capacity&&capacity<=1024);explore_read_calls++;*used=0;
    if(explore_fail_read||request_id!=explore_status.request_id||!explore_status.ready||offset>explore_raw_bytes)return false;
    size_t n=explore_raw_bytes-offset;if(n>capacity)n=capacity;if(explore_read_limit&&n>explore_read_limit)n=explore_read_limit;memcpy(out,explore_raw+offset,n);*used=n;return true;
}
static bool explore_input(uint32_t command,char *value,size_t capacity){
    CHECK(capacity==160&&value);explore_service_calls++;
    if(command==PGA_WEREAD_INPUT_OPEN){explore_input_opens++;return true;}
    if(command==PGA_WEREAD_INPUT_CANCEL){explore_input_ready=false;return true;}
    CHECK(command==PGA_WEREAD_INPUT_TAKE);if(!explore_input_ready)return false;strcpy(value,explore_input_value);explore_input_ready=false;return true;
}
static bool explore_link(const char *url){CHECK(!strcmp(url,"https://weread.qq.com/web/reader/fixture")||!strcmp(url,"https://weread.qq.com/web/reader/fixture-b"));snprintf(explore_opened_link,sizeof(explore_opened_link),"%s",url);explore_link_opens++;return true;}
static bool explore_cover(uint32_t request_id,uint8_t *fb,int x,int y,int width,int height){
    CHECK(request_id==explore_status.request_id&&fb);CHECK(x==20&&y==192&&width==(landscape?64:80)&&height==(landscape?90:112));explore_cover_draws++;return true;
}
static void explore_settle(void){now_us+=1000000;CHECK(app->tick());render();CHECK(!strstr(screen,"返回内容无法读取"));}
static unsigned explore_collect_text(void){
    explore_text_capture[0]=0;explore_text_captured=0;explore_capture_text=true;render();unsigned pages=0;
    while(app->key(2)){render();CHECK(++pages<300);}explore_capture_text=false;return pages;
}
static void explore_check_text(const char *kind,unsigned count,const char *line,const char *ending){
    char expected[100000];unsigned n=0;for(unsigned i=0;i<count;i++)n+=(unsigned)snprintf(expected+n,sizeof(expected)-n,line,kind,i);snprintf(expected+n,sizeof(expected)-n,"%s",ending);CHECK(!strcmp(explore_text_capture,expected));
}
static void explore_open(void){
    start("valid",true);stop();memset(&service_status,0,sizeof(service_status));service_status.bytes=sizeof(service_status);service_status.flags=PGA_WEREAD_CONFIGURED|PGA_WEREAD_WIFI;service_status.revision=1;
    host.weread=weread_service;host.weread_query=explore_query;host.weread_query_read=explore_query_read;host.weread_input=explore_input;host.weread_open_link=explore_link;host.weread_cover=explore_cover;
    memset(&explore_status,0,sizeof(explore_status));explore_status.bytes=sizeof(explore_status);explore_read_limit=0;explore_hold=explore_fail_read=explore_submit_fail=explore_input_ready=false;
    CHECK(app->start(&host));render();tap(view().w-50,35);CHECK(strstr(screen,"在线发现"));
}
static void explore_fixture(void){
    for(unsigned orientation=0;orientation<2;orientation++){
        landscape=orientation;memset(explore_kind_counts,0,sizeof(explore_kind_counts));explore_open();
        // 无IO绘制，搜索只在用户提交后启动，电子书回包scope17仍正常显示。
        // Render has no I/O; search starts after confirmation and accepts ebook group scope17.
        tap(60,130);CHECK(explore_input_opens);strcpy(explore_input_value,"三体");explore_input_ready=true;now_us+=1000000;CHECK(app->tick());render();CHECK(explore_last_request.kind==PGA_WEREAD_SEARCH&&explore_last_request.scope==10&&!strcmp(explore_last_request.keyword,"三体"));
        explore_read_limit=7;explore_settle();CHECK(strstr(screen,"搜索书00"));CHECK(app->key(2));render();CHECK(!strstr(screen,"搜索书00"));CHECK(app->key(0));render();CHECK(strstr(screen,"搜索书00"));explore_read_limit=0;
        tap(55,180);explore_settle();CHECK(strstr(screen,"三体")&&strstr(screen,"刘慈欣"));CHECK(explore_last_request.kind==PGA_WEREAD_INFO);CHECK(explore_cover_draws);tap(view().w-45,35);CHECK(explore_link_opens);
        // 简介和长书评按完整Unicode文本翻页，不在显示缓冲边界切字符。
        // Long introductions/reviews paginate complete Unicode text without splitting display buffers.
        unsigned pages=explore_collect_text();CHECK(strstr(screen,"简介结束")&&pages>5);explore_check_text("简介",180,"%s%03u：完整段落，保留字符😀。","简介结束");CHECK(app->key(0));render();CHECK(!strstr(screen,"简介结束"));
        int width=(view().w-48)/3;tap(16+width+8+20,92);explore_settle();CHECK(explore_last_request.kind==PGA_WEREAD_CHAPTERS);CHECK(explore_raw_bytes>32768&&strstr(screen,"章节0000"));CHECK(app->key(2));render();CHECK(!strstr(screen,"章节0000"));CHECK(app->key(0));render();CHECK(strstr(screen,"章节0000"));
        tap(55,200);explore_settle();CHECK(explore_last_request.kind==PGA_WEREAD_HIGHLIGHTS&&explore_last_request.chapter_uid==1);tap(55,200);CHECK(strstr(screen,"划线000"));pages=explore_collect_text();CHECK(strstr(screen,"划线结束")&&pages>2);explore_check_text("划线",120,"%s%03u：逐字保留。","划线结束");CHECK(app->key(-1));now_us+=1000000;CHECK(app->tick());render();
        tap(16+2*(width+8)+20,92);explore_settle();CHECK(explore_last_request.kind==PGA_WEREAD_PROGRESS);CHECK(strstr(screen,"1%")&&!strstr(screen,"已读完"));CHECK(strstr(screen,"1 小时 1 分钟"));
        tap(16+width+8+20,135);explore_settle();CHECK(explore_last_request.kind==PGA_WEREAD_REVIEWS&&strstr(screen,"书评000"));tap(55,200);pages=explore_collect_text();CHECK(strstr(screen,"书评结束")&&pages>5);explore_check_text("书评",220,"%s%03u：完整文字，逐页显示。","书评结束");CHECK(app->key(-1));now_us+=1000000;CHECK(app->tick());render();CHECK(app->key(2));render();CHECK(explore_last_request.cursor==55&&explore_last_request.synckey==909);explore_settle();
        tap(16+2*(width+8)+20,135);explore_settle();CHECK(explore_last_request.kind==PGA_WEREAD_SIMILAR&&strstr(screen,"相似图书"));tap(55,200);explore_settle();CHECK(explore_last_request.kind==PGA_WEREAD_INFO&&!strcmp(explore_last_request.book_id,"similar-id")&&strstr(screen,"另一部图书"));
        // 返回可恢复上一查询，按重试不会消费来自其他UI的请求。
        // Back restores prior queries and superseded results are never consumed.
        CHECK(app->key(-1));explore_settle();CHECK(strstr(screen,"相似图书")&&strstr(screen,"三体")&&!strstr(screen,"另一部图书"));tap(view().w-45,35);CHECK(!strcmp(explore_opened_link,"https://weread.qq.com/web/reader/fixture"));tap(16+width+8+20,92);explore_settle();CHECK(!strcmp(explore_last_request.book_id,"b0")&&strstr(screen,"三体")&&!strstr(screen,"另一部图书"));
        explore_hold=true;CHECK(app->key(-1));render();explore_status.request_id++;now_us+=1000000;CHECK(app->tick());render();CHECK(strstr(screen,"查询已被另一处操作更新"));explore_hold=false;
        stop();explore_open();tap(view().w-60,250);explore_settle();CHECK(explore_last_request.kind==PGA_WEREAD_STATS);CHECK(strstr(screen,"2 小时 1 分钟")&&strstr(screen,"7 天"));pages=0;while(app->key(2)){render();CHECK(++pages<20);}CHECK(strstr(screen,"100 分钟"));int stats_width=(view().w-44)/4;tap(16+stats_width*3+4*3+15,90);explore_settle();CHECK(explore_last_request.mode==PGA_WEREAD_OVERALL);
        CHECK(app->key(-1));render();tap(45,250);explore_settle();CHECK(explore_last_request.kind==PGA_WEREAD_RECOMMEND&&strstr(screen,"推荐图书"));
        CHECK(app->key(-1));render();explore_submit_fail=true;tap(45,250);CHECK(strstr(screen,"设备尚未联网"));explore_submit_fail=false;tap(40,view().h-96);explore_settle();CHECK(strstr(screen,"推荐图书"));
        CHECK(app->key(-1));render();tap(45,250);explore_fail_read=true;now_us+=1000000;CHECK(app->tick());render();CHECK(strstr(screen,"返回内容无法读取"));explore_fail_read=false;tap(40,view().h-96);explore_settle();CHECK(strstr(screen,"推荐图书"));stop();
        for(unsigned kind=1;kind<=9;kind++)CHECK(explore_kind_counts[kind]);
        // 单worker未完成时返回立即恢复目标页，等旧请求停后再发，不进入busy错误死路。
        // Back restores the destination immediately and queues its request until the one worker is idle.
        explore_open();tap(45,250);explore_settle();explore_hold=true;tap(55,125);CHECK(explore_status.busy&&explore_last_request.kind==PGA_WEREAD_INFO);unsigned rejected=explore_busy_rejects;CHECK(app->key(-1));render();CHECK(strstr(screen,"为你推荐")&&strstr(screen,"正在读取")&&!strstr(screen,"请稍后重试"));now_us+=1000000;(void)app->tick();CHECK(explore_last_request.kind==PGA_WEREAD_INFO&&explore_busy_rejects==rejected);explore_hold=false;explore_settle();explore_settle();CHECK(explore_last_request.kind==PGA_WEREAD_RECOMMEND&&strstr(screen,"推荐图书"));
        // INFO资料先ready，封面仍busy；文字先显示，封面revision只补绘一次。
        // INFO publishes ready text while the cover remains busy; its later revision adds one repaint.
        explore_hold=true;tap(55,125);explore_status.ready=true;explore_status.total_bytes=explore_raw_bytes;explore_status.revision++;explore_settle();CHECK(strstr(screen,"三体")&&explore_status.busy);explore_status.busy=false;explore_status.revision++;explore_settle();
        // 手机另发查询或换Key后，不把旧JSON当作可继续的正文分页。
        // A phone query or account change makes old JSON unavailable without silently advancing text pages.
        explore_status.request_id++;explore_status.ready=false;CHECK(app->key(2));render();CHECK(strstr(screen,"查询已被另一处操作更新"));explore_hold=false;tap(40,view().h-96);explore_settle();CHECK(strstr(screen,"三体")&&!strstr(screen,"查询已被另一处操作更新"));stop();
    }
    host.weread_query=NULL;host.weread_query_read=NULL;host.weread_input=NULL;host.weread_open_link=NULL;host.weread_cover=NULL;
}
