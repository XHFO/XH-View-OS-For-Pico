/* 家庭共享：单条NVS事务与有界可靠分块，阅读控制始终优先。
 * Home sharing: one NVS transaction and bounded reliable chunks; reading control always takes priority. */
#define HOME_SHARED_MAGIC 0x48534831u
#define HOME_SHARE_TAG 240u
static uint32_t share32(const uint8_t *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void share_put32(uint8_t *p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(v>>(i*8));}
static bool s_share_notice,s_setup_seen;
static uint32_t s_tx_seq,s_rx_seq,s_done_seq,s_tx_revision,s_tx_base,s_tx_crc,s_last_setup,s_last_state;
static uint16_t s_tx_n,s_tx_at,s_rx_n,s_rx_at;
static uint8_t s_tx_kind,s_rx_kind,s_ack[9];
static uint32_t s_rx_revision,s_rx_base,s_rx_crc;
static bool s_ack_pending,s_rx_job;
static int64_t s_share_now,s_tx_sent,s_tx_started,s_rx_seen,s_hello_sent;
// 新记录压缩后写入原NVS；临时缓冲在PSRAM，失败不发布。/ Compress into existing NVS using PSRAM scratch; never publish failed writes.
static esp_err_t shared_blob(const char *space,bool writing,home_shared_t *value){
    size_t n=sizeof(s_store->packed);esp_err_t err;
    if(writing){n=rp_home_blob_pack(value,sizeof(*value),s_store->packed,n);err=n?nvs_blob(space,true,s_store->packed,&n):ESP_FAIL;}
    else {err=nvs_blob(space,false,s_store->packed,&n);if(err==ESP_OK&&!rp_home_blob_unpack(s_store->packed,n,value,sizeof(*value)))err=ESP_FAIL;}
    memset(s_store->packed,0,sizeof(s_store->packed));return err;
}
static uint32_t shared_crc(home_shared_t *value){return room_hash(value,offsetof(home_shared_t,checksum));}
static void shared_build(home_shared_t *out,const rp_home_config_t *config,const rp_home_area_scope_t *scope,const rp_home_selection_t *selection){
    memset(out,0,sizeof(*out));out->magic=HOME_SHARED_MAGIC;out->base=s_store->shared.revision;out->revision=out->base+1;
    memcpy(out->owner,s_share_owner,8);memcpy(out->ha,config,HOME_HA_BYTES);
    bool same=memcmp(config->local,s_config.local,offsetof(rp_home_config_t,rooms)-offsetof(rp_home_config_t,local))==0;
    if(scope)out->scope=*scope;else if(same&&scope_ready())out->scope=s_store->room_record.scope;
    if(selection)out->selection=*selection;else if(same&&s_store->selection.binding==room_binding())out->selection=s_store->selection.value;
    out->checksum=shared_crc(out);
}
static bool shared_record_valid(const home_shared_t *value){
    if(value->magic!=HOME_SHARED_MAGIC||value->checksum!=room_hash(value,offsetof(home_shared_t,checksum))||
       !value->revision||value->scope.count>32||value->scope.version>1||value->selection.count>96||value->selection.version>1)return false;
    memcpy(&s_store->edit,&s_config,sizeof(s_config));memcpy(&s_store->edit,value->ha,HOME_HA_BYTES);
    bool ok=rp_home_config_valid(&s_store->edit);memset(&s_store->edit,0,sizeof(s_store->edit));
    for(unsigned i=0;ok&&i<value->scope.count;i++)ok=memchr(value->scope.ids[i],0,64)!=NULL;
    for(unsigned i=0;ok&&i<value->selection.count;i++){
        const char *id=value->selection.ids[i];ok=memchr(id,0,96)&&strchr(id,'.');
        for(const char *p=id;ok&&*p;p++)if(!((*p>='a'&&*p<='z')||(*p>='0'&&*p<='9')||*p=='_'||*p=='.'))ok=false;
        for(unsigned j=0;ok&&j<i;j++)if(!strcmp(id,value->selection.ids[j]))ok=false;
    }
    return ok;
}
static void shared_install(const home_shared_t *value){
    memcpy(&s_config,value->ha,HOME_HA_BYTES);s_store->shared=*value;
    s_store->room_record.binding=room_binding();s_store->room_record.scope=value->scope;
    s_store->room_record.checksum=room_hash(&s_store->room_record,offsetof(__typeof__(s_store->room_record),checksum));
    s_store->selection.binding=room_binding();s_store->selection.value=value->selection;s_store->selection.revision=value->revision;
    s_store->selection.checksum=room_hash(&s_store->selection,offsetof(__typeof__(s_store->selection),checksum));
}
static bool shared_save_locked(const rp_home_config_t *value,const rp_home_area_scope_t *scope,const rp_home_selection_t *selection){
    s_share_deferred=false;shared_build(&s_store->shared_edit,value,scope,selection);
    if(s_share_role==2){
        if(shared_blob("pico_home_draft",true,&s_store->shared_edit)!=ESP_OK)return false;
        s_store->shared_draft=s_store->shared_edit;s_draft_conflict=false;s_draft_pending=s_share_deferred=true;return true;
    }
    if(shared_blob("pico_home_duo",true,&s_store->shared_edit)!=ESP_OK)return false;
    shared_install(&s_store->shared_edit);return true;
}
static void shared_load(void){
    esp_err_t err=shared_blob("pico_home_duo",false,&s_store->shared_edit);
    if(err==ESP_OK&&shared_record_valid(&s_store->shared_edit))shared_install(&s_store->shared_edit);
    else {
        if(err!=ESP_ERR_NVS_NOT_FOUND){memset(&s_config,0,HOME_HA_BYTES);s_config.version=RP_HOME_CONFIG_VERSION;
            memset(&s_store->room_record,0,sizeof(s_store->room_record));memset(&s_store->selection,0,sizeof(s_store->selection));}
        shared_build(&s_store->shared,&s_config,NULL,NULL);s_store->shared.base=0;s_store->shared.checksum=shared_crc(&s_store->shared);
    }
    if(shared_blob("pico_home_draft",false,&s_store->shared_draft)==ESP_OK&&shared_record_valid(&s_store->shared_draft))
        {s_draft_pending=s_store->shared_draft.base!=UINT32_MAX;s_draft_conflict=!s_draft_pending;}
    memset(&s_store->shared_edit,0,sizeof(s_store->shared_edit));
}
static void run_share(void){
    lock();if(!s_share_job||s_suspended||s_home_reading){unlock();return;}
    home_shared_t *value=&s_store->shared_in;
    bool ok=s_share_link&&shared_record_valid(value)&&!s_portal_owner&&!read_pico_management_active();
    if(ok&&s_share_role==1){
        ok=value->base==s_store->shared.revision&&!memcmp(value->owner,s_share_owner,8);
        if(ok){value->revision=s_store->shared.revision+1;value->checksum=shared_crc(value);}
    }else if(ok)ok=s_share_role==2&&!memcmp(value->owner,s_share_owner,8)&&
        (memcmp(s_store->shared.owner,value->owner,8)||value->revision>=s_store->shared.revision);
    if(ok)ok=shared_blob("pico_home_duo",true,value)==ESP_OK;
    if(ok){
        bool same_draft=!memcmp(value->ha,s_store->shared_draft.ha,HOME_HA_BYTES)&&
            !memcmp(&value->scope,&s_store->shared_draft.scope,sizeof(value->scope))&&
            !memcmp(&value->selection,&s_store->shared_draft.selection,sizeof(value->selection));
        if(s_share_role==2&&s_draft_pending&&(same_draft||value->revision!=s_store->shared_draft.base||memcmp(value->owner,s_store->shared_draft.owner,8))){
            s_draft_pending=false;s_share_notice=!same_draft;s_draft_conflict=!same_draft;
            // 冲突草稿持久保留但不再自动提交；确认成功才清除。/ Persist conflicts without auto-submitting; clear only accepted drafts.
            if(same_draft){uint32_t tombstone=0;size_t n=sizeof(tombstone);(void)nvs_blob("pico_home_draft",true,&tombstone,&n);}
            else {s_store->shared_draft.base=UINT32_MAX;s_store->shared_draft.checksum=shared_crc(&s_store->shared_draft);
                (void)shared_blob("pico_home_draft",true,&s_store->shared_draft);}
        }
        shared_install(value);config_changed_locked();
        s_setup_seen=true;
    }
    if(s_share_role==1)s_last_setup=0;
    s_share_result=ok?1:0;s_share_job=false;memset(value,0,sizeof(*value));unlock();
}
static void share_reset(void){
    s_tx_n=s_rx_n=s_tx_at=s_rx_at=0;s_tx_kind=0;s_ack_pending=s_rx_job=false;
    s_last_setup=s_last_state=0;s_tx_sent=s_hello_sent=0;s_done_seq=s_rx_seq=0;s_setup_seen=false;s_peer_home=s_peer_home_reading=false;s_share_link=false;
    if(s_store){memset(s_store->share_tx,0,sizeof(s_store->share_tx));memset(s_store->share_rx,0,sizeof(s_store->share_rx));}
}
static void share_ack(uint32_t seq,uint16_t offset,uint8_t result){
    s_ack[0]=HOME_SHARE_TAG;s_ack[1]=3;share_put32(s_ack+2,seq);s_ack[6]=(uint8_t)offset;s_ack[7]=(uint8_t)(offset>>8);s_ack[8]=result;s_ack_pending=true;
}
typedef struct {uint32_t revision,config_revision,count,result;int32_t http_status;rp_home_entity_t entities[96];} home_shared_state_t;
_Static_assert(sizeof(home_shared_state_t)<HOME_SHARE_LIMIT,"bounded home state");
static void share_start(uint8_t kind){
    if(!s_store||s_tx_n)return;
    lock();s_tx_kind=kind;s_tx_base=0;
    if(kind==1||kind==2){
        home_shared_t *value=kind==1?&s_store->shared:&s_store->shared_draft;
        memcpy(s_store->share_tx,value,sizeof(*value));s_tx_n=sizeof(*value);s_tx_revision=value->revision;s_tx_base=value->base;
    }else{
        home_shared_state_t *value=(void*)s_store->share_tx;
        value->revision=s_snap.revision;value->config_revision=s_store->shared.revision;value->count=s_snap.entity_count;
        value->result=s_snap.result;value->http_status=s_snap.http_status;
        memcpy(value->entities,s_snap.entities,value->count*sizeof(value->entities[0]));
        s_tx_n=(uint16_t)(offsetof(home_shared_state_t,entities)+value->count*sizeof(value->entities[0]));s_tx_revision=value->revision;
    }
    s_tx_started=s_share_now;s_tx_crc=room_hash(s_store->share_tx,s_tx_n);s_tx_at=UINT16_MAX;s_tx_sent=0;if(++s_tx_seq==0)s_tx_seq=1;unlock();
}
void ps_home_pga_share_context(int role,bool reading){lock();s_share_role=role;s_home_local_reading=reading;s_home_reading=reading||s_peer_home_reading;unlock();}
void ps_home_pga_share_tick(int role,bool connected,const uint8_t owner[8],int64_t now,bool reading){
    static bool had_link;static int old_role;static uint8_t old_owner[8];
    s_share_now=now;ps_home_pga_share_context(role,reading);reading=s_home_reading;
    if(!connected||old_role!=role||memcmp(old_owner,owner,8)){
        if(had_link||old_role!=role){share_reset();if(s_store){lock();s_share_job=false;unlock();}}
        had_link=false;old_role=role;memcpy(old_owner,owner,8);
    }
    memcpy(s_share_owner,owner,8);
    if(!connected)return;
    ps_home_pga_init();if(!s_ready)return;
    if(!had_link){had_link=true;share_reset();}
    lock();
    if(role==1&&memcmp(s_store->shared.owner,owner,8)){
        s_store->shared_edit=s_store->shared;memcpy(s_store->shared_edit.owner,owner,8);s_store->shared_edit.revision++;
        s_store->shared_edit.checksum=shared_crc(&s_store->shared_edit);
        if(shared_blob("pico_home_duo",true,&s_store->shared_edit)==ESP_OK)shared_install(&s_store->shared_edit);
    }
    bool home=now<s_home_visible_until||s_peer_home,notify=false;
    if(!reading&&role==1&&home&&!s_busy&&now>=s_home_poll_due&&scope_ready()&&s_config.enabled){
        read_pico_connectivity_link_t net={0};read_pico_connectivity_get_link(&net);
        if(net.online){s_online=true;s_at_home=*s_config.home_wifi&&!strcmp(net.ssid,s_config.home_wifi);s_queued=s_busy=true;notify=true;}
        s_home_poll_due=now+30000;
    }
    if(s_share_job&&!reading)notify=true;
    bool waiting=s_share_job;
    int result=s_share_result;
    unlock();if(notify)xTaskNotifyGive(s_worker);
    if(s_rx_job&&!waiting&&result>=0){share_ack(s_rx_seq,s_rx_n,result?1:2);s_done_seq=s_rx_seq;s_rx_n=0;s_rx_job=false;memset(s_store->share_rx,0,sizeof(s_store->share_rx));}
    if(s_share_notice){s_share_notice=false;ps_toast("家庭设置已采用主机版本，离线草稿未覆盖主机");}
    if(s_tx_n&&now-s_tx_started>90000&&!reading){memset(s_store->share_tx,0,s_tx_n);s_tx_n=0;s_tx_sent=0;s_hello_sent=0;}
    if(reading&&s_tx_n)s_tx_started=now;
    if(!reading&&s_share_link&&!s_tx_n){
        if(role==1&&s_last_setup!=s_store->shared.revision)share_start(1);
        else if(role==2&&s_setup_seen&&s_draft_pending)share_start(2);
        else if(role==1&&home&&s_last_state!=s_snap.revision)share_start(3);
    }
    if(s_rx_n&&!s_rx_job&&now-s_rx_seen>90000){s_rx_n=0;memset(s_store->share_rx,0,sizeof(s_store->share_rx));}
}
bool ps_home_pga_share_receive(const uint8_t *p,size_t n){
    if(!p||!n||p[0]!=HOME_SHARE_TAG)return false;
    if(!s_ready||n<2)return true;
    if(p[1]==4){
        if(n!=16||p[2]!=1)return true;
        s_share_link=true;s_peer_home=(p[3]&1)!=0;s_peer_home_reading=(p[3]&4)!=0;
        if(share32(p+4)==s_store->shared.revision&&share32(p+12)==s_store->shared.checksum){
            if(s_share_role==1)s_last_setup=s_store->shared.revision;else s_setup_seen=true;
        }
        if((p[3]&2)&&s_share_role==1)s_home_poll_due=0;
        return true;
    }
    if(!s_share_link)return true;
    if(p[1]==3){
        if(n!=9||!s_tx_n||share32(p+2)!=s_tx_seq)return true;
        uint16_t offset=(uint16_t)(p[6]|p[7]<<8);
        if(p[8]){
            if(p[8]==1){if(s_tx_kind==1)s_last_setup=s_tx_revision;else if(s_tx_kind==3)s_last_state=s_tx_revision;}
            if(s_tx_kind==2){if(p[8]==2){s_share_notice=true;}s_setup_seen=false;}
            memset(s_store->share_tx,0,s_tx_n);s_tx_n=0;s_tx_sent=0;return true;
        }
        if(s_tx_at==UINT16_MAX&&offset==0){s_tx_at=0;s_tx_sent=0;}
        else if(s_tx_at!=UINT16_MAX&&offset>s_tx_at&&offset<=s_tx_n&&offset-s_tx_at<=80){s_tx_at=offset;s_tx_sent=0;}
        return true;
    }
    if(p[1]==0){
        if(n!=22||p[3]!=1||s_home_reading)return true;
        uint16_t bytes=(uint16_t)(p[16]|p[17]<<8);uint32_t seq=share32(p+4);
        if(!bytes||bytes>HOME_SHARE_LIMIT||!seq||p[2]<1||p[2]>3||(s_share_role==1&&p[2]!=2)||(s_share_role==2&&p[2]==2))return true;
        if(seq==s_done_seq){share_ack(seq,bytes,1);return true;}
        if(seq==s_rx_seq&&s_rx_n){share_ack(seq,s_rx_at,0);return true;}
        if(s_rx_job)return true;
        s_rx_seq=seq;s_rx_n=bytes;s_rx_at=0;s_rx_kind=p[2];s_rx_base=share32(p+8);s_rx_revision=share32(p+12);s_rx_crc=share32(p+18);
        memset(s_store->share_rx,0,sizeof(s_store->share_rx));s_rx_seen=s_share_now;share_ack(seq,0,0);return true;
    }
    if(n<6||share32(p+2)!=s_rx_seq)return true;
    if(s_rx_seq==s_done_seq){share_ack(s_rx_seq,0,1);return true;}
    if(p[1]==1){
        if(n<9||p[8]>80||n!=9u+p[8]||!s_rx_n)return true;
        uint16_t offset=(uint16_t)(p[6]|p[7]<<8);
        if(offset==s_rx_at&&s_rx_at+p[8]<=s_rx_n){memcpy(s_store->share_rx+s_rx_at,p+9,p[8]);s_rx_at+=p[8];}
        s_rx_seen=s_share_now;share_ack(s_rx_seq,s_rx_at,0);return true;
    }
    if(p[1]!=2||n!=6||s_rx_at!=s_rx_n||s_rx_job||s_home_reading)return true;
    if(room_hash(s_store->share_rx,s_rx_n)!=s_rx_crc){share_ack(s_rx_seq,s_rx_n,2);s_rx_n=0;return true;}
    if(s_rx_kind==1||s_rx_kind==2){
        if(s_rx_n!=sizeof(home_shared_t)){share_ack(s_rx_seq,s_rx_n,2);s_rx_n=0;return true;}
        home_shared_t *value=(void*)s_store->share_rx;
        if(value->base!=s_rx_base||value->revision!=s_rx_revision){share_ack(s_rx_seq,s_rx_n,2);s_rx_n=0;return true;}
        lock();s_store->shared_in=*value;s_share_job=s_rx_job=true;s_share_result=-1;unlock();xTaskNotifyGive(s_worker);
    }else{
        home_shared_state_t *value=(void*)s_store->share_rx;bool ok=s_rx_n>=offsetof(home_shared_state_t,entities)&&value->count<=96&&value->result<=RP_HOME_ACCESS&&
            s_rx_n==offsetof(home_shared_state_t,entities)+value->count*sizeof(rp_home_entity_t)&&value->config_revision==s_store->shared.revision;
        for(unsigned i=0;ok&&i<value->count;i++){
            const rp_home_entity_t *e=&value->entities[i];ok=e->kind<=RP_HOME_HUMIDITY&&e->available<=1&&e->on<=1&&
                memchr(e->id,0,sizeof(e->id))&&memchr(e->name,0,sizeof(e->name))&&memchr(e->state,0,sizeof(e->state))&&
                memchr(e->unit,0,sizeof(e->unit))&&memchr(e->room,0,sizeof(e->room));
        }
        if(ok){lock();memcpy(s_snap.entities,value->entities,value->count*sizeof(rp_home_entity_t));s_snap.entity_count=s_snap.entity_total=value->count;
            s_snap.result=value->result;s_snap.http_status=value->http_status;s_snap.sampled_ms=now_ms();s_snap.revision++;unlock();s_share_invalidate=false;}
        share_ack(s_rx_seq,s_rx_n,ok?1:2);s_done_seq=s_rx_seq;s_rx_n=0;memset(s_store->share_rx,0,sizeof(s_store->share_rx));
    }
    return true;
}
size_t ps_home_pga_share_take(uint8_t *p,size_t capacity){
    if(!s_ready||!p||capacity<96)return 0;
    if(s_ack_pending){memcpy(p,s_ack,9);s_ack_pending=false;return 9;}
    if(!s_hello_sent||s_share_now-s_hello_sent>=1500){
        p[0]=HOME_SHARE_TAG;p[1]=4;p[2]=1;p[3]=(s_share_now<s_home_visible_until?1:0)|(s_share_invalidate?2:0)|(s_home_local_reading?4:0);
        share_put32(p+4,s_store->shared.revision);share_put32(p+8,s_snap.revision);share_put32(p+12,s_store->shared.checksum);s_hello_sent=s_share_now;return 16;
    }
    if(s_home_reading||!s_tx_n||(s_tx_sent&&s_share_now-s_tx_sent<400))return 0;
    p[0]=HOME_SHARE_TAG;s_tx_sent=s_share_now;
    if(s_tx_at==UINT16_MAX){p[1]=0;p[2]=s_tx_kind;p[3]=1;share_put32(p+4,s_tx_seq);share_put32(p+8,s_tx_base);share_put32(p+12,s_tx_revision);
        p[16]=(uint8_t)s_tx_n;p[17]=(uint8_t)(s_tx_n>>8);share_put32(p+18,s_tx_crc);return 22;}
    share_put32(p+2,s_tx_seq);
    if(s_tx_at==s_tx_n){p[1]=2;return 6;}
    unsigned count=s_tx_n-s_tx_at;if(count>80)count=80;p[1]=1;p[6]=(uint8_t)s_tx_at;p[7]=(uint8_t)(s_tx_at>>8);p[8]=(uint8_t)count;
    memcpy(p+9,s_store->share_tx+s_tx_at,count);return 9+count;
}
