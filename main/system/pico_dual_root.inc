/* 根页联动只交换小型UI状态；不打开书籍、不写阅读进度。 / Root pairing exchanges small UI state only; it never opens books or writes reading progress. */
typedef struct {
    uint32_t epoch,revision,layout;
    uint16_t page,count,ack,request;
    uint8_t tag,version,kind,action;
} root_packet_t;
_Static_assert(sizeof(root_packet_t)==24,"root packet wire size");
static root_packet_t root_local={.tag=241,.version=1},root_peer;
static int64_t root_seen,root_sent,root_called;
static bool root_dirty,root_linked;
static uint16_t root_request;
static bool root_pair(void){
    return status.connected&&status.synced&&root_local.kind&&root_peer.kind==root_local.kind&&
           root_peer.count==root_local.count&&ps_now()-root_seen<4000;
}
static unsigned root_last(unsigned count){return count?count-1:0;}
static bool root_layout_valid(unsigned kind,uint32_t value){
    return kind==1?!(value&~63u)&&(value&7u)<5&&(value>>4)<3:kind==2?value<=63:!value;
}
void ps_dual_root_leave(void){
    if(root_local.kind){root_local.kind=0;root_local.action=0;root_local.layout=0;root_local.count=0;root_dirty=true;root_linked=false;}
}
int ps_dual_root_page(unsigned kind,int page,int count,uint32_t *layout,int action){
    if(!layout||kind<1||kind>2||count<1||count>4096||!root_layout_valid(kind,*layout))return page;
    root_called=ps_now();
    if(!root_local.epoch)root_local.epoch=esp_random()|1u;
    bool entered=root_local.kind!=kind;
    bool changed=entered||root_local.count!=count;
    if(root_local.kind!=kind){root_local.page=(uint16_t)page;root_local.action=0;root_linked=false;}
    root_local.kind=kind;root_local.count=count;
    if(settings.role==1){
        if(!entered&&root_linked&&action==0)*layout=root_local.layout;
        if(root_local.layout!=*layout){root_local.layout=*layout;root_local.page=(uint16_t)page;changed=true;}
        if(root_local.page>root_last(count)){root_local.page=root_last(count);changed=true;}
    }
    if(settings.role==2&&status.connected&&root_peer.kind==kind&&ps_now()-root_seen<4000&&action!=3)*layout=root_peer.layout;
    bool paired=root_pair();
    if(paired&&settings.role==2){
        if(action==3||action==1||action==2){
            root_local.action=action;root_local.request=++root_request;
            root_local.epoch=root_peer.epoch;root_local.revision=root_peer.revision;
            root_local.layout=action==3?*layout:root_peer.layout;changed=true;
        }else if(!root_local.action){root_local.layout=root_peer.layout;}
        *layout=root_peer.layout;
    }else if(paired&&settings.role==1&&(action==1||action==2)){
        int next=(int)root_local.page+(action==1?2:-2);
        if(next<0)next=0;
        if((unsigned)next>root_last(count))next=root_local.page;
        if(next!=root_local.page){root_local.page=next;changed=true;}
    }else if(!paired&&root_local.page!=page){root_local.page=(uint16_t)page;changed=true;}
    if(changed){if(settings.role==1)root_local.revision++;root_dirty=true;}
    if(paired!=root_linked){root_linked=paired;root_dirty=true;ps_dirty();}
    if(!paired)return page<0?0:page>=count?count-1:page;
    return settings.role==2?root_peer.page+1:root_local.page;
}
bool ps_dual_root_active(void){return root_linked&&root_pair();}
static bool root_receive(const uint8_t *data,size_t bytes){
    if(!bytes||data[0]!=241)return false;
    if(bytes!=sizeof(root_packet_t))return true;
    root_packet_t value;memcpy(&value,data,sizeof(value));
    // 线格式以tag开头；其余字段用固定偏移打包，避免ABI填充。/ Tag precedes the fixed-offset payload, avoiding ABI padding.
    value.tag=data[0];value.version=data[1];value.kind=data[2];value.action=data[3];
    memcpy(&value.epoch,data+4,20);
    if(value.version!=1||value.kind>2||value.count>4096||value.action>3||
       !root_layout_valid(value.kind,value.layout))return true;
    if(settings.role==2&&root_peer.epoch==value.epoch&&(int32_t)(value.revision-root_peer.revision)<0)return true;
    bool changed=value.kind!=root_peer.kind||value.page!=root_peer.page||value.layout!=root_peer.layout||value.epoch!=root_peer.epoch;
    root_peer=value;root_seen=ps_now();
    if(settings.role==1&&root_pair()&&value.epoch==root_local.epoch&&value.action&&value.request!=root_local.ack){
        if(value.revision==root_local.revision){
            if(value.action==3){root_local.layout=value.layout;root_local.page=0;}
            else{
                int next=(int)root_local.page+(value.action==1?2:-2);
                root_local.page=(uint16_t)(next<0?0:(unsigned)next>root_last(root_local.count)?root_local.page:(unsigned)next);
            }
        }
        root_local.ack=value.request;root_local.revision++;root_dirty=true;changed=true;
    }
    if(settings.role==2&&root_local.request==value.ack)root_local.action=0;
    if(changed)ps_dirty();
    return true;
}
static size_t root_take(uint8_t *data,size_t capacity){
    if(!root_local.kind&&!root_dirty&&ps_now()-root_called>4000)return 0;
    if(capacity<24||(!root_dirty&&ps_now()-root_sent<1200))return 0;
    data[0]=241;data[1]=1;data[2]=root_local.kind;data[3]=root_local.action;
    memcpy(data+4,&root_local.epoch,20);root_dirty=false;root_sent=ps_now();return 24;
}
static void root_tick(const app_desc_t *app){
    if(!status.connected){memset(&root_peer,0,sizeof(root_peer));root_linked=false;root_local.action=0;root_sent=0;}
    if(app==&app_book||!app->product_shell||
       (ps_route()!=PS_SHELF&&ps_route()!=PS_APPLICATION)||ps_now()-root_called>1500)ps_dual_root_leave();
}

#ifdef PICO_NATIVE_PREVIEW
void ps_dual_root_native(unsigned *kind,unsigned *page,unsigned *pages){
    *kind=root_local.kind;*page=ps_dual_root_active()&&settings.role==2?root_peer.page+1:root_local.page;*pages=root_local.count;
}
#endif
