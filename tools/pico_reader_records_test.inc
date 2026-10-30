/* SPDX-License-Identifier: Apache-2.0
 * 中文：动态阅读记录、旧格式和跨存储合并的故障注入验证；仅供主机测试。
 * English: Host fault-injection checks for dynamic records, legacy formats and cross-store merge.
 */
static uint64_t bank_hash(blob_t *bank,const char *prefix){
    uint64_t hash=UINT64_C(14695981039346656037);size_t n=strlen(prefix);
    for(int i=0;i<MOCK_BLOBS;i++)if(bank[i].key[0]&&!strncmp(bank[i].key,prefix,n)){
        for(const char *k=bank[i].key;*k;k++)hash=(hash^(uint8_t)*k)*UINT64_C(1099511628211);
        for(size_t j=0;j<bank[i].size;j++)hash=(hash^((uint8_t*)bank[i].bytes)[j])*UINT64_C(1099511628211);
    }
    return hash;
}
static void records_test_reset(void){
    mock_filesystem=false;mock_current_is_source=false;editor_note=editor_real_note=editor_dirty=false;route=PS_SHELF;depth=0;
    blobs_clear(blobs);blobs_clear(source_blobs);reload_reader_no_writes();
    assert(rr&&!rr->count&&!records_read_failed);max_record_allocation=0;
}
static ps_book_anchor_t records_test_anchor(uint32_t i){
    return (ps_book_anchor_t){.path="/sdcard/books/records.epub",.title="记录容量验证",.file_bytes=123456,.chapter=2,.start=i*3,.end=i*3+3,.quote="原文"};
}
static void make_v2_fixture(blob_t *bank,bool trailing_full){
    reader_record_index_t index={.version=2,.count=70,.sequence=7,.slot=1};
    assert(blob_store(bank,"reader-r2-index",&index,sizeof(index)));
    reader_record_chunk_t *chunk=calloc(1,sizeof(*chunk));assert(chunk);
    for(uint32_t i=0;i<3;i++){
        *chunk=(reader_record_chunk_t){.version=2,.sequence=7,.index=i,.count=i==2?6:32};
        for(uint32_t j=0;j<chunk->count;j++){
            ps_book_anchor_t a=records_test_anchor(i*32+j);reader_record_v2_t *n=&chunk->item[j];
            n->book=REAL_BOOK;n->type=j%2?REC_NOTE:REC_HIGHLIGHT;n->offset=a.start;n->end=a.end;n->file_bytes=a.file_bytes;n->chapter=a.chapter;
            strcpy(n->quote,a.quote);strcpy(n->title,a.title);strcpy(n->path,a.path);
            if(n->type==REC_NOTE){memset(n->text,'n',383);n->text[383]=0;n->ink_count=1;n->ink[0]=(reader_ink_t){0x8000|25,30};}
        }
        char key[40];snprintf(key,sizeof(key),"reader-r2-b-%u",i);
        size_t bytes=trailing_full?sizeof(*chunk):offsetof(reader_record_chunk_t,item)+chunk->count*sizeof(chunk->item[0]);
        assert(blob_store(bank,key,chunk,bytes));
    }
    free(chunk);
}
static void make_v3_fixture(blob_t *bank,uint32_t count,uint32_t anchor_start){
    reader_index_v3_t *index=calloc(1,sizeof(*index));assert(index);
    *index=(reader_index_v3_t){.version=3,.count=count,.sequence=9,.next_id=count+1};
    for(uint32_t i=0;i<count;i++){
        ps_book_anchor_t a=records_test_anchor(anchor_start+i);size_t bytes=sizeof(reader_object_v3_t)+1;
        reader_object_v3_t *n=calloc(1,bytes);assert(n);n->version=3;n->text_bytes=1;n->book=REAL_BOOK;n->type=REC_HIGHLIGHT;n->offset=a.start;n->end=a.end;n->file_low=(uint32_t)a.file_bytes;n->chapter=a.chapter;
        strcpy(n->quote,a.quote);strcpy(n->title,a.title);strcpy(n->path,a.path);
        char key[40];snprintf(key,sizeof(key),"reader-r3-o-%u",i+1);assert(blob_store(bank,key,n,bytes));
        index->object[i]=(reader_object_ref_t){i+1,(uint32_t)bytes};free(n);
    }
    assert(blob_store(bank,"reader-r3-index",index,sizeof(*index)));free(index);
}
static void test_records_new_target(void){
    records_test_reset();
    for(int i=0;i<2;i++){snprintf(mock_fs_roots[i],sizeof(mock_fs_roots[i]),"%s/migrate-%d",rootdir,i);assert(!mkdir(mock_fs_roots[i],0700));}
    mock_filesystem=true;ps_book_anchor_t a=records_test_anchor(20);assert(ps_reader_real_highlight(&a));
    ps_reader_real_note(&a);assert(editor_append("旧根里的完整批注"));editor_commit();assert(rr->count==2);
    ps_book_anchor_t pending=records_test_anchor(40);ps_reader_real_note(&pending);assert(editor_append("换卡后仍可恢复的未完成草稿")&&draft_save());ps_back();
    uint32_t old_ids[2]={rr->item[0]->object_id,rr->item[1]->object_id},old_next=records_next_id,old_sequence=records_sequence;
    reader_real_draft_t kept_draft=real_draft;char kept_editor[3072];strcpy(kept_editor,editor_text);uint64_t source_hash=mock_fs_hash(mock_fs_roots[0],"");
    mock_current_is_source=true;
    for(int stage=0;stage<3;stage++){
        char key[40];snprintf(key,sizeof(key),"reader-r3-o-%u",old_next+(stage==1));fail_store_key=stage<2?key:"reader-r3-index";
        assert(!ps_reader_storage_save());fail_store_key=NULL;
        assert(rr->count==2&&rr->item[0]->object_id==old_ids[0]&&rr->item[1]->object_id==old_ids[1]&&records_next_id==old_next&&records_sequence==old_sequence);
        assert(mock_fs_hash(mock_fs_roots[0],"")==source_hash&&!memcmp(&real_draft,&kept_draft,sizeof(real_draft))&&!strcmp(editor_text,kept_editor));
        bool index_exists=true;assert(mock_fs_exists(mock_fs_roots[1],"reader-r3-index",&index_exists)&&!index_exists);
    }
    assert(ps_reader_storage_save()&&rr->item[0]->object_id==old_next&&rr->item[1]->object_id==old_next+1&&mock_fs_hash(mock_fs_roots[0],"")==source_hash);
    uint64_t target_objects=mock_fs_hash(mock_fs_roots[1],"reader-r3-o-");uint32_t target_next=records_next_id;
    assert(ps_reader_power_save()&&mock_fs_hash(mock_fs_roots[1],"reader-r3-o-")==target_objects&&records_next_id==target_next);
    // 完全移除旧根后只从新目标重载，不能借用源对象或RAM正文。 / Remove the source completely and reload only the new target, without borrowing source objects or RAM text.
    mock_fs_clear(mock_fs_roots[0]);reload_reader_no_writes();assert(rr->count==2&&!records_read_failed&&rr->item[0]->type==REC_HIGHLIGHT&&!strcmp(rr->item[1]->text,"旧根里的完整批注"));
    assert(rr->item[0]->offset==a.start&&rr->item[1]->end==a.end&&!strcmp(real_draft.text,kept_draft.text));
    ps_reader_real_note(&pending);assert(!strcmp(editor_text,kept_editor));ps_back();mock_fs_clear(mock_fs_roots[1]);records_test_reset();
    puts("PASS: two real temporary filesystem roots with length/CRC framing; new-target migration rewrites every immutable object before index, object/index failures preserve source IDs/draft, retry reloads without source; power checkpoint keeps object bytes.");
}
static void test_expanded_records_and_merge(void){
    ps_reader_records_import_result_t result;
    for(int full=0;full<2;full++){
        records_test_reset();make_v2_fixture(blobs,full);uint64_t old=bank_hash(blobs,"reader-r2");reload_reader_no_writes();
        assert(rr->count==70&&!records_read_failed&&strlen(rr->item[1]->text)==383&&rr->item[1]->ink[0].y==30);
        assert(records_save()&&bank_hash(blobs,"reader-r2")==old);reload_reader_no_writes();assert(rr->count==70&&!records_read_failed);
        make_v2_fixture(source_blobs,full);old=bank_hash(source_blobs,"");int writes=stores;
        assert(ps_reader_records_import("/mock/source",&result)&&!result.added&&result.duplicates==70&&result.total_count==70&&stores==writes&&bank_hash(source_blobs,"")==old);
    }
    // 显式读取第一代源记录，精确重复去重且整个源库保持不变。 / Explicitly import v1 source entries with exact deduplication and no source writes.
    records_test_reset();
    typedef struct {uint32_t version,count;reader_legacy_record_t item[LEGACY_RECORD_MAX];} merge_legacy_t;
    merge_legacy_t *legacy=calloc(1,sizeof(*legacy));assert(legacy);legacy->version=1;legacy->count=2;
    legacy->item[0]=(reader_legacy_record_t){.book=3,.type=REC_HIGHLIGHT,.offset=0,.end=3};strcpy(legacy->item[0].quote,"旧划线");
    legacy->item[1]=(reader_legacy_record_t){.book=3,.type=REC_NOTE,.offset=3,.end=3};strcpy(legacy->item[1].text,"源库旧笔记");
    assert(blob_store(source_blobs,"reader-records-v1",legacy,sizeof(*legacy)));uint64_t legacy_hash=bank_hash(source_blobs,"");
    assert(ps_reader_records_import("/mock/source",&result)&&result.source_count==2&&result.added==2&&result.total_count==2&&bank_hash(source_blobs,"")==legacy_hash);
    assert(ps_reader_records_import("/mock/source",&result)&&result.duplicates==2&&!result.added&&bank_hash(source_blobs,"")==legacy_hash);
    free(legacy);
    // 旧草稿只读转换，保留源字节和笔迹；新草稿保存到独立的新键。 / Convert old drafts read-only, preserving bytes/ink and writing new drafts under separate keys.
    records_test_reset();reader_draft_v1_t olddraft={.version=1,.book=3,.offset=0,.end=0,.ink_count=1};
    strcpy(olddraft.text,"旧示例草稿");strcpy(olddraft.quote,"旧引用");olddraft.ink[0]=(reader_ink_t){0x8000|12,23};
    reader_real_draft_v1_t oldreal={.version=1,.anchor=records_test_anchor(4),.ink_count=1};strcpy(oldreal.text,"旧真实草稿");oldreal.ink[0]=(reader_ink_t){0x8000|45,67};
    assert(ps_store("reader-draft-v1",&olddraft,sizeof(olddraft))&&ps_store("reader-real-d1",&oldreal,sizeof(oldreal)));uint64_t draft_hash=bank_hash(blobs,"");
    reload_reader_no_writes();assert(!strcmp(draft.text,olddraft.text)&&draft.ink[0].y==23&&!strcmp(real_draft.text,oldreal.text)&&real_draft.ink[0].y==67&&bank_hash(blobs,"")==draft_hash);
    ps_reader_real_note(&oldreal.anchor);assert(editor_limit==2047&&!strcmp(editor_text,oldreal.text));editor_text[0]=0;
    char note[2049];for(int i=0;i<682;i++)memcpy(note+i*3,"汉",3);note[2046]='a';note[2047]=0;
    assert(strlen(note)==2047&&editor_append(note)&&!editor_append("汉")&&draft_save());uint32_t count=rr->count;editor_commit();
    assert(rr->count==count+1&&!strcmp(rr->item[count]->text,note));reload_reader_no_writes();assert(!strcmp(rr->item[count]->text,note));
    reader_record_t invalid=*rr->item[count];note[2047]='x';note[2048]=0;invalid.text=note;int writes=stores;assert(!add_record(&invalid)&&stores==writes);
    note[2046]=(char)0xe6;note[2047]=0;invalid.text=note;assert(!valid_record(&invalid));
    reader_draft_v1_t oldcheck;reader_real_draft_v1_t realcheck;
    assert(ps_load("reader-draft-v1",&oldcheck,sizeof(oldcheck))&&!memcmp(&oldcheck,&olddraft,sizeof(oldcheck))&&ps_load("reader-real-d1",&realcheck,sizeof(realcheck))&&!memcmp(&realcheck,&oldreal,sizeof(realcheck)));
    // v3源ID与目标ID重合，新增记录仍分配目标的新ID，源库不改动。 / Colliding source IDs receive fresh target IDs without modifying the source.
    records_test_reset();ps_book_anchor_t a=records_test_anchor(0);assert(ps_reader_real_highlight(&a));make_v3_fixture(source_blobs,2,1);
    uint64_t source_hash=bank_hash(source_blobs,"");reader_record_t *first=rr->item[0];uint64_t committed=bank_hash(blobs,"reader-r3-index");
    for(int stage=0;stage<3;stage++){
        fail_store_after=stage;assert(!ps_reader_records_import("/mock/source",&result));fail_store_after=-1;
        assert(result.status==PS_READER_IMPORT_SAVE_FAILED&&rr->count==1&&rr->item[0]==first&&bank_hash(blobs,"reader-r3-index")==committed&&bank_hash(source_blobs,"")==source_hash);
        reload_reader_no_writes();assert(rr->count==1&&!records_read_failed);first=rr->item[0];
    }
    for(int failure=0;failure<7;failure++){
        allocation_fail_after=failure;bool imported=ps_reader_records_import("/mock/source",&result);allocation_fail_after=-1;
        assert(!imported&&(result.status==PS_READER_IMPORT_MEMORY||result.status==PS_READER_IMPORT_SAVE_FAILED)&&rr->count==1&&rr->item[0]==first&&bank_hash(blobs,"reader-r3-index")==committed);
    }
    assert(ps_reader_records_import("/mock/source",&result)&&result.added==2&&result.total_count==3&&rr->item[0]->object_id==1&&rr->item[1]->object_id==2&&rr->item[2]->object_id==3&&bank_hash(source_blobs,"")==source_hash);
    writes=stores;assert(ps_reader_records_import("/mock/source",&result)&&result.duplicates==2&&!result.added&&stores==writes);
    // 真实退出编辑只保留可恢复草稿；存储页可继续合并，重启后仍可恢复未完成批注。
    // Leaving the actual editor retains its resumable draft while the storage route can merge; reboot must still recover it.
    ps_book_anchor_t unfinished=records_test_anchor(100);ps_reader_real_note(&unfinished);
    assert(route==PS_EDITOR&&editor_note&&editor_real_note&&editor_append("尚未提交的批注草稿"));
    assert(!ps_reader_records_import("/mock/source",&result)&&result.status==PS_READER_IMPORT_BUSY);
    ps_back();ps_navigate(PS_STORAGE);assert(route==PS_STORAGE&&editor_note&&!editor_dirty);
    char kept_draft[PS_READER_NOTE_MAX_BYTES+1];strcpy(kept_draft,real_draft.text);
    uint64_t draft_before=bank_hash(blobs,"reader-real-d2");writes=stores;
    assert(ps_reader_records_import("/mock/source",&result)&&result.duplicates==2&&!result.added&&stores==writes&&bank_hash(blobs,"reader-real-d2")==draft_before);
    assert(!strcmp(real_draft.text,kept_draft));reload_reader_no_writes();
    ps_reader_real_note(&unfinished);assert(route==PS_EDITOR&&!strcmp(editor_text,kept_draft)&&editor_note&&editor_real_note);
    assert(!ps_reader_records_import("/mock/source",&result)&&result.status==PS_READER_IMPORT_BUSY);
    ps_back();ps_navigate(PS_STORAGE);
    // 新源记录在有离页草稿时也可提交；草稿和编辑缓存不得被清除或改写。
    // New entries may commit beside an exited draft without clearing or rewriting its text/cache.
    blobs_clear(source_blobs);make_v3_fixture(source_blobs,1,8);uint32_t existing_count=rr->count;
    assert(ps_reader_records_import("/mock/source",&result)&&result.added==1&&rr->count==existing_count+1&&bank_hash(blobs,"reader-real-d2")==draft_before&&!strcmp(editor_text,kept_draft));
    selected_record=(int)rr->count-1;record_delete();assert(rr->count==existing_count);
    blobs_clear(source_blobs);make_v3_fixture(source_blobs,2,1);route=PS_SHELF;
    record_roots_unavailable=true;assert(!ps_reader_records_import("/mock/source",&result)&&result.status==PS_READER_IMPORT_UNAVAILABLE);record_roots_unavailable=false;
    assert(!ps_reader_records_import("/evil",&result)&&result.status==PS_READER_IMPORT_UNAVAILABLE);
    // 损坏的后续对象不能提交前面已读取的新记录。 / A corrupt later object must not publish earlier streamed additions.
    blobs_clear(source_blobs);make_v3_fixture(source_blobs,2,4);blob_t *source_object=blob_find(source_blobs,"reader-r3-o-2");assert(source_object);reader_object_v3_t *disk=source_object->bytes;
    for(int malformed=0;malformed<5;malformed++){
        uint32_t version=disk->version,text_bytes=disk->text_bytes;uint8_t reserved=disk->reserved;char text=((char*)(disk+1))[0];
        if(malformed==0)disk->version=4;else if(malformed==1)disk->reserved=1;else if(malformed==2)disk->text_bytes=0;else if(malformed==3)((char*)(disk+1))[0]='x';else source_object->size--;
        source_hash=bank_hash(source_blobs,"");count=rr->count;committed=bank_hash(blobs,"reader-r3-index");
        assert(!ps_reader_records_import("/mock/source",&result)&&result.status==PS_READER_IMPORT_CORRUPT&&rr->count==count&&bank_hash(blobs,"reader-r3-index")==committed&&bank_hash(source_blobs,"")==source_hash);
        disk->version=version;disk->reserved=reserved;disk->text_bytes=text_bytes;((char*)(disk+1))[0]=text;if(malformed==4)source_object->size++;
    }
    blob_t *source_index=blob_find(source_blobs,"reader-r3-index");assert(source_index);reader_index_v3_t *index=source_index->bytes;
    for(int malformed=0;malformed<4;malformed++){
        reader_object_ref_t saved=index->object[1];uint32_t saved_count=index->count;
        if(malformed==0)index->count=RECORD_MAX+1;else if(malformed==1)index->object[1].id=1;else if(malformed==2)index->object[1].id=index->next_id;else index->object[1].bytes=sizeof(reader_object_v3_t);
        assert(!ps_reader_records_import("/mock/source",&result)&&result.status==PS_READER_IMPORT_CORRUPT&&rr->count==3);index->count=saved_count;index->object[1]=saved;
    }
    size_t bad_bytes=sizeof(reader_object_v3_t)+2;reader_object_v3_t *bad=calloc(1,bad_bytes);assert(bad);memcpy(bad,disk,sizeof(*bad));bad->text_bytes=2;((char*)(bad+1))[0]=(char)0xff;
    index->object[1].bytes=(uint32_t)bad_bytes;assert(blob_store(source_blobs,"reader-r3-o-2",bad,bad_bytes));free(bad);
    assert(!ps_reader_records_import("/mock/source",&result)&&result.status==PS_READER_IMPORT_CORRUPT&&rr->count==3);
    blob_remove(source_blobs,"reader-r3-o-2");assert(!ps_reader_records_import("/mock/source",&result)&&result.status==PS_READER_IMPORT_CORRUPT&&rr->count==3);
    blobs_clear(source_blobs);record_exists_failed=true;assert(!ps_reader_records_import("/mock/source",&result)&&result.status==PS_READER_IMPORT_UNAVAILABLE);record_exists_failed=false;
    assert(!ps_reader_records_import("/mock/source",&result)&&result.status==PS_READER_IMPORT_NO_SOURCE);
    // 实际填满1024条，重复合并允许零新增；超限与新对象分配失败不改变旧库。 / Fill 1024 real entries; duplicate-only imports succeed and limit/allocation failures retain the library.
    records_test_reset();max_record_allocation=0;
    for(uint32_t i=0;i<RECORD_MAX;i++){a=records_test_anchor(i);writes=stores;assert(ps_reader_real_highlight(&a)&&stores==writes+2);}
    assert(rr->count==1024&&max_record_allocation<65536);reload_reader_no_writes();assert(rr->count==1024&&!records_read_failed);
    a=records_test_anchor(1024);writes=stores;assert(!ps_reader_real_highlight(&a)&&stores==writes&&rr->count==1024);
    make_v3_fixture(source_blobs,1,0);source_hash=bank_hash(source_blobs,"");assert(ps_reader_records_import("/mock/source",&result)&&!result.added&&result.duplicates==1&&result.total_count==1024&&bank_hash(source_blobs,"")==source_hash);
    blobs_clear(source_blobs);make_v3_fixture(source_blobs,1,2048);assert(!ps_reader_records_import("/mock/source",&result)&&result.status==PS_READER_IMPORT_LIMIT&&rr->count==1024);
    selected_record=500;record_delete();assert(rr->count==1023);reload_reader_no_writes();assert(rr->count==1023&&!records_read_failed);
    first=rr->item[0];allocation_fail_after=0;a=records_test_anchor(3000);assert(!ps_reader_real_highlight(&a)&&rr->count==1023&&rr->item[0]==first);allocation_fail_after=-1;
    records_test_reset();test_records_new_target();
    puts("PASS: actual 1024 records, 2047-byte UTF-8 notes, bounded allocations, dev39 compact/full chunks and old drafts preserved; v3 ID collisions, source-preserving/idempotent merge, partial corruption, capacity and allocation/object/index rollback.");
}
