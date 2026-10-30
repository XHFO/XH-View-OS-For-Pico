/* SPDX-License-Identifier: Apache-2.0
 * 中文：可选 TF 离线词库，仅进入输入设置或编辑器时加载；逐键查询只读有界 RAM。
 * English: Optional TF lexicon loaded only on input-settings/editor entry; keystrokes query bounded RAM.
 * 冻结：词库最多 2 MiB，无法载入时回退内置词库，已载入词库在失卡时可继续使用；不写用户卡。
 * Frozen: Limit lexicons to 2 MiB, fall back when loading fails; retain loaded RAM data across media loss, never write the card.
 */
#ifndef PICO_IME_DICTIONARY_PATH
#define PICO_IME_DICTIONARY_PATH "/sdcard/readpico/ime/pinyin-v1.bin"
#endif
static uint8_t *ime_dictionary;
static uint32_t ime_dictionary_count,ime_dictionary_bytes,ime_dictionary_generation;
static bool ime_dictionary_attempted;
static void ime_dictionary_release(void){
    heap_caps_free(ime_dictionary);ime_dictionary=NULL;
    ime_dictionary_count=ime_dictionary_bytes=0;ime_dictionary_attempted=false;
}
static uint32_t ime_u32(const uint8_t *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static const uint8_t *ime_entry(uint32_t index,bool t9){
    const uint8_t *indices=ime_dictionary+(t9?ime_dictionary_count*4:0);
    uint32_t offset=ime_u32(indices+index*4);
    return ime_dictionary+ime_dictionary_count*8+offset;
}
static int ime_dictionary_compare(const char *word,const char *key,bool t9){
    for(;*word&&*key;word++,key++){
        int a=t9?t9digit(*word):*word;
        if(a!=(unsigned char)*key)return a-(unsigned char)*key;
    }
    return (unsigned char)*word-(unsigned char)*key;
}
static PICO_READER_COLD void ime_dictionary_prepare(void){
    read_pico_sd_info_t info;
    if(ps_storage_busy()||ps_usb_held()||read_pico_sd_get_info(&info)||!info.present||!info.mounted||info.exported)return;
    if(ime_dictionary_attempted&&ime_dictionary_generation==info.media_generation)return;
    ime_dictionary_release();ime_dictionary_attempted=true;ime_dictionary_generation=info.media_generation;
    FILE *f=fopen(PICO_IME_DICTIONARY_PATH,"rb");if(!f)return;
    uint8_t header[32];uint8_t *blob=NULL;bool ok=fread(header,1,sizeof(header),f)==sizeof(header)&&!memcmp(header,"PICOIME1",8);
    uint32_t count=ok?ime_u32(header+8):0,bytes=ok?ime_u32(header+12):0;
    ok=ok&&count&&count<=65535&&bytes>count*8&&bytes<=2*1024*1024;
    if(ok){
        blob=heap_caps_calloc(bytes,1,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
        // 临时内存不足不永久禁用同一张卡的扩展词库；仅下次明确进入时重试。
        // Transient OOM must not disable a card's lexicon permanently; retry only on the next explicit entry.
        if(!blob){ime_dictionary_attempted=false;fclose(f);return;}
        ok=fread(blob,1,bytes,f)==bytes&&fgetc(f)==EOF;
    }
    fclose(f);
    uint32_t hash=2166136261U;
    if(ok){for(uint32_t i=0;i<bytes;i++)hash=(hash^blob[i])*16777619U;ok=hash==ime_u32(header+16);}
    const uint8_t *data=blob?blob+count*8:NULL;uint32_t data_bytes=bytes-count*8,previous=0;
    for(uint32_t i=0;ok&&i<count;i++){
        uint32_t offset=ime_u32(blob+i*4),other=ime_u32(blob+(count+i)*4);
        if(offset>=data_bytes||other>=data_bytes||offset+4>=data_bytes){ok=false;break;}
        const uint8_t *entry=data+offset;unsigned plen=entry[0],tlen=entry[1];
        if(!plen||plen>39||!tlen||tlen>=CAND_TEXT||offset+4+plen+tlen>=data_bytes||entry[3+plen]||entry[4+plen+tlen]||!u8valid((const char*)entry+4+plen,tlen)){ok=false;break;}
        for(unsigned j=0;j<plen;j++)if(entry[3+j]<'a'||entry[3+j]>'z')ok=false;
        if(i&&(offset<=previous||strcmp((const char*)data+previous+3,(const char*)entry+3)>0))ok=false;
        previous=offset;
    }
    if(!ok){heap_caps_free(blob);return;}
    ime_dictionary=blob;ime_dictionary_count=count;ime_dictionary_bytes=bytes;
    // 九键索引必须指向合法条目并有序，避免不可信卡片越界。 / Validate T9 entries/order to prevent malformed-card reads.
    for(uint32_t i=0;i<count;i++){
        uint32_t offset=ime_u32(blob+(count+i)*4),lo=0,hi=count;
        while(lo<hi){uint32_t mid=lo+(hi-lo)/2;if(ime_u32(blob+mid*4)<offset)lo=mid+1;else hi=mid;}
        if(lo==count||ime_u32(blob+lo*4)!=offset){ok=false;break;}
        if(i){
            const char *a=(const char*)ime_entry(i-1,true)+3,*b=(const char*)ime_entry(i,true)+3;
            while(*a&&*b&&t9digit(*a)==t9digit(*b)){a++;b++;}
            if((*a?t9digit(*a):0)>(*b?t9digit(*b):0)){ok=false;break;}
        }
    }
    if(!ok){heap_caps_free(ime_dictionary);ime_dictionary=NULL;ime_dictionary_count=ime_dictionary_bytes=0;}
}
static void ime_dictionary_tokens(int start,int length){
    if(!ime_dictionary)return;
    bool t9=input_mode==INPUT_T9;char key[40];
    for(int end=start+1;end<=length;end++){
        if(end>start+1&&ime_boundary[end-1])break;
        size_t size=(size_t)(end-start);memcpy(key,ime_input+start,size);key[size]=0;
        uint32_t lo=0,hi=ime_dictionary_count;
        while(lo<hi){uint32_t mid=lo+(hi-lo)/2;if(ime_dictionary_compare((const char*)ime_entry(mid,t9)+3,key,t9)<0)lo=mid+1;else hi=mid;}
        for(uint32_t at=lo,seen=0;at<ime_dictionary_count&&seen<32;at++,seen++){
            const uint8_t *entry=ime_entry(at,t9);const char *pinyin=(const char*)entry+3;
            if(ime_dictionary_compare(pinyin,key,t9))break;
            reader_pinyin_t token={pinyin,pinyin+entry[0]+1};
            ime_token(&token,true,(int)entry[2]*100,start,length);
        }
    }
    // 末尾不完整音节只检查一个有界前缀窗口，常用词按频率排名。 / Complete the unfinished tail with a bounded prefix window and frequency ranking.
    int remaining=length-start;
    if(remaining<2)return;
    memcpy(key,ime_input+start,(size_t)remaining);key[remaining]=0;
    uint32_t lo=0,hi=ime_dictionary_count;
    while(lo<hi){uint32_t mid=lo+(hi-lo)/2;if(ime_dictionary_compare((const char*)ime_entry(mid,t9)+3,key,t9)<0)lo=mid+1;else hi=mid;}
    for(uint32_t at=lo,seen=0;at<ime_dictionary_count&&seen<96;at++,seen++){
        const uint8_t *entry=ime_entry(at,t9);const char *pinyin=(const char*)entry+3;int same=0;
        while(same<remaining&&pinyin[same]&&(t9?t9digit(pinyin[same]):pinyin[same])==key[same])same++;
        if(same<remaining)break;
        if(entry[0]==remaining||entry[0]-remaining>4)continue;
        reader_pinyin_t token={pinyin,pinyin+entry[0]+1};ime_token(&token,true,(int)entry[2]*100,start,length);
    }
}
