/* SPDX-License-Identifier: Apache-2.0
 * 中文：有界阅读记录对象、旧格式读取及显式保留源库的合并；只由pico_reader.c包含。
 * English: Bounded record objects, legacy reads and explicit source-preserving merging; included only by pico_reader.c.
 * 冻结：正文不组成大数组；对象先写索引最后提交；源库只读，失败不替换运行库或丢编辑内容。
 * Frozen: No rectangular text array; write objects before committing the index, never write the source or replace live/editor state on failure.
 */
typedef struct { uint32_t id,bytes; } reader_object_ref_t;
typedef struct {
    uint32_t version,count,sequence,next_id;
    reader_object_ref_t object[RECORD_MAX];
} reader_index_v3_t;
typedef struct {
    uint32_t version,offset,end,chapter,file_low,file_high,text_bytes;
    uint8_t book,type,ink_count,reserved;
    char quote[512],path[BOOK_STORE_PATH_MAX],title[96];
    reader_ink_t ink[INK_MAX];
} reader_object_v3_t;
typedef struct {
    uint32_t version,offset,end;
    uint8_t book,ink_count,reserved[2];
    char text[384],quote[244];
    reader_ink_t ink[INK_MAX];
} reader_draft_v1_t;
typedef struct {
    uint32_t version;
    ps_book_anchor_t anchor;
    char text[384];
    reader_ink_t ink[INK_MAX];
    uint32_t ink_count;
} reader_real_draft_v1_t;
_Static_assert(sizeof(reader_index_v3_t)<=9*1024,"Reader index must remain bounded");
_Static_assert(sizeof(reader_object_v3_t)<2048,"Reader object metadata must remain small");

static bool real_record(const ps_book_anchor_t *a,int type,reader_record_t *n);
static void records_clear_from(reader_records_t *library,uint32_t first){
    while(library&&library->count>first)heap_caps_free(library->item[--library->count]);
}
static void records_free(reader_records_t *library){records_clear_from(library,0);heap_caps_free(library);}
static reader_record_t *record_clone(const reader_record_t *source){
    const char *text=source->text?source->text:"";size_t n=strlen(text)+1;
    reader_record_t *copy=heap_caps_calloc(1,sizeof(*copy)+n,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(copy){*copy=*source;copy->text=(char *)(copy+1);memcpy(copy->text,text,n);}
    return copy;
}
static bool records_equal(const reader_record_t *a,const reader_record_t *b){
    return a->type==b->type&&a->book==b->book&&a->offset==b->offset&&a->end==b->end&&
        a->ink_count==b->ink_count&&!strcmp(a->text?a->text:"",b->text?b->text:"")&&!strcmp(a->quote,b->quote)&&
        !memcmp(a->ink,b->ink,a->ink_count*sizeof(a->ink[0]))&&
        (a->book!=REAL_BOOK||(a->file_bytes==b->file_bytes&&a->chapter==b->chapter&&!strcmp(a->path,b->path)&&!strcmp(a->title,b->title)));
}
static bool records_blob_load(const char *root,const char *key,void *out,size_t bytes){
    char current[160],other[160];bool sd;
    if(!root){if(!ps_storage_record_roots(current,other,sizeof(current),&sd))return false;root=current;}
    return ps_load_at(root,key,out,bytes);
}
static bool records_exists(const char *root,const char *key,bool *exists){
    char current[160],other[160];bool sd;
    if(!root){if(!ps_storage_record_roots(current,other,sizeof(current),&sd))return false;root=current;}
    return ps_store_exists_at(root,key,exists);
}
static bool records_snapshot_accept(const reader_record_t *n);
static bool records_accept(reader_records_t *library,const reader_record_t *n,bool merge,ps_reader_records_import_result_t *result){
    if(!valid_record(n)){result->status=PS_READER_IMPORT_CORRUPT;return false;}
    if(!library)return records_snapshot_accept(n);
    if(merge){
        for(uint32_t i=0;i<library->count;i++)if(records_equal(n,library->item[i])){result->duplicates++;return true;}
    }
    if(library->count>=RECORD_MAX){result->status=PS_READER_IMPORT_LIMIT;return false;}
    reader_record_t *copy=record_clone(n);
    if(!copy){result->status=PS_READER_IMPORT_MEMORY;return false;}
    if(merge)copy->object_id=copy->object_bytes=0;
    library->item[library->count++]=copy;
    if(merge)result->added++;
    return true;
}
static void record_from_v2(reader_record_t *out,const reader_record_v2_t *in){
    memset(out,0,sizeof(*out));out->offset=in->offset;out->end=in->end;out->book=in->book;out->type=in->type;out->ink_count=in->ink_count;
    memcpy(out->quote,in->quote,sizeof(out->quote));out->text=(char *)in->text;memcpy(out->ink,in->ink,sizeof(out->ink));
    memcpy(out->path,in->path,sizeof(out->path));memcpy(out->title,in->title,sizeof(out->title));out->file_bytes=in->file_bytes;out->chapter=in->chapter;
}
// 源记录逐条验证、去重和分配，不同时驻留两个满库。 / Validate, deduplicate and allocate source entries one at a time instead of holding two full libraries.
static bool records_read(const char *root,reader_records_t *library,bool merge,ps_reader_records_import_result_t *result){
    bool exists=false;result->status=PS_READER_IMPORT_OK;
    if(!records_exists(root,"reader-r3-index",&exists)){result->status=PS_READER_IMPORT_UNAVAILABLE;return false;}
    if(exists){
        reader_index_v3_t *index=heap_caps_calloc(1,sizeof(*index),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
        if(!index){result->status=PS_READER_IMPORT_MEMORY;return false;}
        bool ok=records_blob_load(root,"reader-r3-index",index,sizeof(*index))&&index->version==3&&index->count<=RECORD_MAX&&index->sequence&&index->next_id;
        result->source_count=index->count;
        for(uint32_t i=0;ok&&i<index->count;i++){
            reader_object_ref_t ref=index->object[i];
            ok=ref.id&&ref.id<index->next_id&&ref.bytes>sizeof(reader_object_v3_t)&&ref.bytes<=sizeof(reader_object_v3_t)+PS_READER_NOTE_MAX_BYTES+1;
            for(uint32_t j=0;ok&&j<i;j++)if(index->object[j].id==ref.id)ok=false;
            if(!ok)break;
            reader_object_v3_t *disk=heap_caps_calloc(1,ref.bytes,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
            if(!disk){result->status=PS_READER_IMPORT_MEMORY;ok=false;break;}
            char key[40];snprintf(key,sizeof(key),"reader-r3-o-%lu",(unsigned long)ref.id);
            ok=records_blob_load(root,key,disk,ref.bytes)&&disk->version==3&&!disk->reserved&&disk->text_bytes==ref.bytes-sizeof(*disk);
            char *text=(char *)(disk+1);
            if(ok)ok=memchr(text,0,disk->text_bytes)==text+disk->text_bytes-1;
            if(ok){
                reader_record_t n={.offset=disk->offset,.end=disk->end,.book=disk->book,.type=disk->type,.ink_count=disk->ink_count,
                    .file_bytes=(uint64_t)disk->file_low|((uint64_t)disk->file_high<<32),.chapter=disk->chapter,.object_id=ref.id,.object_bytes=ref.bytes,.text=text};
                memcpy(n.quote,disk->quote,sizeof(n.quote));memcpy(n.path,disk->path,sizeof(n.path));memcpy(n.title,disk->title,sizeof(n.title));memcpy(n.ink,disk->ink,sizeof(n.ink));
                ok=records_accept(library,&n,merge,result);
            }
            heap_caps_free(disk);
        }
        if(ok&&!merge){records_sequence=index->sequence;records_next_id=index->next_id;}
        heap_caps_free(index);
        if(!ok&&result->status==PS_READER_IMPORT_OK)result->status=PS_READER_IMPORT_CORRUPT;
        return ok;
    }
    if(!records_exists(root,"reader-r2-index",&exists)){result->status=PS_READER_IMPORT_UNAVAILABLE;return false;}
    if(exists){
        reader_record_index_t index={0};
        bool ok=records_blob_load(root,"reader-r2-index",&index,sizeof(index))&&index.version==2&&index.count<=256&&index.slot<2&&index.sequence;
        result->source_count=index.count;
        reader_record_chunk_t *chunk=ok?heap_caps_calloc(1,sizeof(*chunk),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT):NULL;
        if(ok&&!chunk){result->status=PS_READER_IMPORT_MEMORY;return false;}
        for(uint32_t i=0;ok&&i<(index.count+RECORD_CHUNK-1)/RECORD_CHUNK;i++){
            char key[40];snprintf(key,sizeof(key),"reader-r2-%c-%lu",index.slot?'b':'a',(unsigned long)i);
            uint32_t expected=index.count-i*RECORD_CHUNK;if(expected>RECORD_CHUNK)expected=RECORD_CHUNK;
            size_t bytes=offsetof(reader_record_chunk_t,item)+expected*sizeof(chunk->item[0]);
            ok=(records_blob_load(root,key,chunk,bytes)||(bytes!=sizeof(*chunk)&&records_blob_load(root,key,chunk,sizeof(*chunk))))&&
                chunk->version==2&&chunk->sequence==index.sequence&&chunk->index==i&&chunk->count==expected;
            for(uint32_t j=0;ok&&j<expected;j++){
                if(!memchr(chunk->item[j].text,0,sizeof(chunk->item[j].text))){ok=false;break;}
                reader_record_t n;record_from_v2(&n,&chunk->item[j]);ok=records_accept(library,&n,merge,result);
            }
        }
        heap_caps_free(chunk);
        if(ok&&!merge){records_sequence=index.sequence;records_slot=index.slot;}
        if(!ok&&result->status==PS_READER_IMPORT_OK)result->status=PS_READER_IMPORT_CORRUPT;
        return ok;
    }
    if(!records_exists(root,"reader-records-v1",&exists)){result->status=PS_READER_IMPORT_UNAVAILABLE;return false;}
    if(!exists){result->status=PS_READER_IMPORT_NO_SOURCE;return true;}
    typedef struct {uint32_t version,count;reader_legacy_record_t item[LEGACY_RECORD_MAX];} legacy_t;
    legacy_t *old=heap_caps_calloc(1,sizeof(*old),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!old){result->status=PS_READER_IMPORT_MEMORY;return false;}
    bool ok=records_blob_load(root,"reader-records-v1",old,sizeof(*old))&&old->version==1&&old->count<=LEGACY_RECORD_MAX;
    result->source_count=old->count;
    for(uint32_t i=0;ok&&i<old->count;i++){
        reader_legacy_record_t *a=&old->item[i];reader_record_t n={.offset=a->offset,.end=a->end,.book=a->book,.type=a->type,.ink_count=a->ink_count,.text=a->text};
        if(!memchr(a->quote,0,sizeof(a->quote))||!memchr(a->text,0,sizeof(a->text))){ok=false;break;}
        memcpy(n.quote,a->quote,sizeof(a->quote));memcpy(n.ink,a->ink,sizeof(n.ink));ok=records_accept(library,&n,merge,result);
    }
    heap_caps_free(old);
    if(!ok&&result->status==PS_READER_IMPORT_OK)result->status=PS_READER_IMPORT_CORRUPT;
    return ok;
}
static PICO_READER_COLD void records_load(void){
    records_sequence=records_slot=0;records_next_id=1;records_read_failed=false;rr->version=3;
    ps_reader_records_import_result_t result={0};
    if(!records_read(NULL,rr,false,&result)){records_clear_from(rr,0);records_read_failed=true;ps_toast("阅读记录读取失败，原文件保留，请检查存储");}
}
static bool records_commit(reader_records_t *library,bool rewrite_objects){
    if(!library||library->count>RECORD_MAX||records_read_failed)return false;
    reader_index_v3_t *index=heap_caps_calloc(1,sizeof(*index),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!index)return false;
    index->version=3;index->count=library->count;index->sequence=records_sequence+1;if(!index->sequence)index->sequence=1;
    uint32_t next=records_next_id?records_next_id:1;bool ok=true;
    for(uint32_t i=0;ok&&i<library->count;i++){
        reader_record_t *n=library->item[i];
        if(!valid_record(n)){ok=false;break;}
        if(n->object_id&&!rewrite_objects){index->object[i]=(reader_object_ref_t){n->object_id,n->object_bytes};continue;}
        if(next==UINT32_MAX){ok=false;break;}
        size_t text_bytes=strlen(n->text)+1,bytes=sizeof(reader_object_v3_t)+text_bytes;
        reader_object_v3_t *disk=heap_caps_calloc(1,bytes,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
        if(!disk){ok=false;break;}
        disk->version=3;disk->offset=n->offset;disk->end=n->end;disk->chapter=n->chapter;disk->file_low=(uint32_t)n->file_bytes;disk->file_high=(uint32_t)(n->file_bytes>>32);
        disk->text_bytes=(uint32_t)text_bytes;disk->book=n->book;disk->type=n->type;disk->ink_count=n->ink_count;
        memcpy(disk->quote,n->quote,sizeof(disk->quote));memcpy(disk->path,n->path,sizeof(disk->path));memcpy(disk->title,n->title,sizeof(disk->title));memcpy(disk->ink,n->ink,sizeof(disk->ink));memcpy(disk+1,n->text,text_bytes);
        char key[40];snprintf(key,sizeof(key),"reader-r3-o-%lu",(unsigned long)next);
        ok=reader_store(key,disk,bytes);heap_caps_free(disk);index->object[i]=(reader_object_ref_t){next++,(uint32_t)bytes};
    }
    index->next_id=next;
    // 任一对象失败时旧索引保持可读；只有索引提交成功后才给运行对象赋予持久ID。
    // Object failure leaves the old index readable; publish persistent IDs to live objects only after the final index succeeds.
    if(ok)ok=reader_store("reader-r3-index",index,sizeof(*index));
    if(ok){
        for(uint32_t i=0;i<library->count;i++){library->item[i]->object_id=index->object[i].id;library->item[i]->object_bytes=index->object[i].bytes;}
        records_sequence=index->sequence;records_next_id=next;
    }
    heap_caps_free(index);return ok;
}
static PICO_READER_COLD bool records_save(void){
    if(records_read_failed){ps_toast("原有记录读取异常，未覆盖；请重新检测存储");return false;}
    if(!records_commit(rr,false)){ps_toast("阅读记录未能写入，内容仍保留在本次会话");return false;}
    return true;
}
bool ps_reader_records_import(const char *source_root,ps_reader_records_import_result_t *output){
    ps_reader_records_import_result_t result={.status=PS_READER_IMPORT_UNAVAILABLE};
    char current[160],other[160];bool sd;
    if(!rr||!initialized||!source_root||!ps_storage_record_roots(current,other,sizeof(current),&sd)||strcmp(source_root,other)||!other[0])goto done;
    result.target_count=result.total_count=rr->count;
    // 草稿标记可在离开编辑页后保留；仅活动编辑页占用输入，不能把可恢复草稿误判为仍在编辑。
    // Draft ownership may survive leaving the editor; only the active editor route blocks importing.
    if(ps_storage_busy()||ps_usb_held()||ps_route()==PS_EDITOR){result.status=PS_READER_IMPORT_BUSY;goto done;}
    if(records_read_failed){result.status=PS_READER_IMPORT_CORRUPT;goto done;}
    reader_records_t *merged=heap_caps_calloc(1,sizeof(*merged),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!merged){result.status=PS_READER_IMPORT_MEMORY;goto done;}
    *merged=*rr;
    bool ok=records_read(source_root,merged,true,&result);
    if(ok&&result.status!=PS_READER_IMPORT_NO_SOURCE){
        if(!result.added||records_commit(merged,false)){
            rr->count=merged->count;memcpy(rr->item,merged->item,rr->count*sizeof(rr->item[0]));
            result.status=PS_READER_IMPORT_OK;result.total_count=rr->count;selected_record=-1;record_scroll=0;
        }else{result.status=PS_READER_IMPORT_SAVE_FAILED;ok=false;}
    }
    if(!ok)records_clear_from(merged,result.target_count);
    heap_caps_free(merged);
done:
    if(output)*output=result;
    return result.status==PS_READER_IMPORT_OK;
}
// 快照逐条读取并写入非活动缓冲区，不同时驻留两份完整记录库。/ Stream records into the inactive bank instead of holding two complete libraries in memory.
static struct {const char *root;reader_index_v3_t *index;} records_snapshot;
static bool records_snapshot_accept(const reader_record_t *n){
    if(!records_snapshot.index||records_snapshot.index->count>=RECORD_MAX)return false;
    size_t text_bytes=strlen(n->text)+1,bytes=sizeof(reader_object_v3_t)+text_bytes;
    reader_object_v3_t *disk=heap_caps_calloc(1,bytes,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!disk)return false;
    disk->version=3;disk->offset=n->offset;disk->end=n->end;disk->chapter=n->chapter;disk->file_low=(uint32_t)n->file_bytes;disk->file_high=(uint32_t)(n->file_bytes>>32);
    disk->text_bytes=(uint32_t)text_bytes;disk->book=n->book;disk->type=n->type;disk->ink_count=n->ink_count;
    memcpy(disk->quote,n->quote,sizeof(disk->quote));memcpy(disk->path,n->path,sizeof(disk->path));memcpy(disk->title,n->title,sizeof(disk->title));memcpy(disk->ink,n->ink,sizeof(disk->ink));memcpy(disk+1,n->text,text_bytes);
    uint32_t id=records_snapshot.index->count+1;char key[40];snprintf(key,sizeof(key),"reader-r3-o-%lu",(unsigned long)id);
    bool ok=ps_reading_snapshot_store(records_snapshot.root,key,disk,bytes,NULL);heap_caps_free(disk);
    if(ok)records_snapshot.index->object[records_snapshot.index->count++]=(reader_object_ref_t){id,(uint32_t)bytes};
    return ok;
}
PICO_READER_COLD bool ps_reader_records_export(const char *source_root,const char *target_root,uint32_t sequence,uint32_t *index_crc){
    if(!target_root||!sequence||!index_crc||records_snapshot.index)return false;
    reader_index_v3_t *index=heap_caps_calloc(1,sizeof(*index),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!index)return false;
    *index=(reader_index_v3_t){.version=3,.sequence=sequence};records_snapshot.root=target_root;records_snapshot.index=index;
    ps_reader_records_import_result_t result={0};
    bool ok=!source_root||!*source_root||records_read(source_root,NULL,true,&result);
    index->next_id=index->count+1;
    if(ok)ok=ps_reading_snapshot_store(target_root,"reader-r3-index",index,sizeof(*index),index_crc);
    records_snapshot.index=NULL;records_snapshot.root=NULL;heap_caps_free(index);return ok;
}
static void drafts_load(void){
    memset(&draft,0,sizeof(draft));memset(&real_draft,0,sizeof(real_draft));
    bool exists=false,loaded=records_blob_load(NULL,"reader-draft-v2",&draft,sizeof(draft));
    if(!loaded&&records_exists(NULL,"reader-draft-v2",&exists)&&!exists){
        reader_draft_v1_t *old=heap_caps_calloc(1,sizeof(*old),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
        if(old&&records_blob_load(NULL,"reader-draft-v1",old,sizeof(*old))&&memchr(old->text,0,sizeof(old->text))){
            draft.version=old->version;draft.offset=old->offset;draft.end=old->end;draft.book=old->book;draft.ink_count=old->ink_count;
            memcpy(draft.quote,old->quote,sizeof(draft.quote));memcpy(draft.ink,old->ink,sizeof(draft.ink));ri_copy(draft.text,sizeof(draft.text),old->text);
        }
        heap_caps_free(old);
    }
    if(draft.version!=1||draft.book>=BOOK_COUNT||draft.ink_count>INK_MAX||!memchr(draft.text,0,sizeof(draft.text))||!memchr(draft.quote,0,sizeof(draft.quote))||
       (rb[draft.book].comic?(draft.offset>=8||draft.end!=draft.offset):(draft.end>rb[draft.book].bytes||draft.offset>draft.end))||!u8valid(draft.text,strlen(draft.text))){memset(&draft,0,sizeof(draft));draft.version=1;}
    for(uint32_t i=0;i<draft.ink_count;i++)if((draft.ink[i].x&0x7fff)>1023||draft.ink[i].y>1023){memset(&draft,0,sizeof(draft));draft.version=1;break;}
    loaded=records_blob_load(NULL,"reader-real-d2",&real_draft,sizeof(real_draft));exists=false;
    if(!loaded&&records_exists(NULL,"reader-real-d2",&exists)&&!exists){
        reader_real_draft_v1_t *old=heap_caps_calloc(1,sizeof(*old),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
        if(old&&records_blob_load(NULL,"reader-real-d1",old,sizeof(*old))&&memchr(old->text,0,sizeof(old->text))){
            real_draft.version=old->version;real_draft.anchor=old->anchor;real_draft.ink_count=old->ink_count;
            memcpy(real_draft.ink,old->ink,sizeof(real_draft.ink));ri_copy(real_draft.text,sizeof(real_draft.text),old->text);
        }
        heap_caps_free(old);
    }
    bool valid=real_draft.version==1&&real_draft.ink_count<=INK_MAX&&memchr(real_draft.text,0,sizeof(real_draft.text))&&u8valid(real_draft.text,strlen(real_draft.text));
    if(valid&&real_draft.anchor.path[0]){reader_record_t probe;valid=real_record(&real_draft.anchor,REC_NOTE,&probe);}
    for(uint32_t i=0;valid&&i<real_draft.ink_count;i++)if((real_draft.ink[i].x&0x7fff)>1023||real_draft.ink[i].y>1023)valid=false;
    if(!valid){memset(&real_draft,0,sizeof(real_draft));real_draft.version=1;}
}
