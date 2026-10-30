/* SPDX-License-Identifier: Apache-2.0
 * 中文：真实阅读器电源检查点的零调用快路径与故障重试；只在主机运行。
 * English: Actual-reader power checkpoint no-call paths and fault retries; host-only.
 */
static void checkpoint_idle_no_store(void){
    int before=stores;uint32_t sequence=records_sequence;uint64_t index=bank_hash(blobs,"reader-r3-index");
    assert(ps_reader_power_save()&&reader_checkpoint_clean&&stores==before&&records_sequence==sequence&&bank_hash(blobs,"reader-r3-index")==index);
}
static void test_reader_power_checkpoints(void){
    records_test_reset();
    free(shelf_favorites);shelf_favorites=NULL;shelf_favorites_loaded=shelf_favorites_failed=false;shelf_favorites_load();
    assert(sizeof(reader_prefs_t)==248&&sizeof(reader_draft_t)==2564&&sizeof(reader_real_draft_t)==3240&&sizeof(reader_index_v3_t)==8208&&sizeof(reader_favorites_t)==55304);
    assert(!reader_checkpoint_clean&&shelf_favorites&&!shelf_favorites_failed);
    int before=stores;assert(ps_reader_power_save()&&reader_checkpoint_clean&&stores==before+5);
    checkpoint_idle_no_store();checkpoint_idle_no_store();
    ps_edit("普通输入","",32,NULL);assert(editor_append("临时搜索")&&editor_dirty&&!editor_note);ps_back();checkpoint_idle_no_store();

    // 成功的即时保存无需在锁屏时再次读CRC或提交索引。 / Successful immediate saves need neither CRC reads nor another index commit on lock.
    rp.sort=!rp.sort;assert(prefs_save());checkpoint_idle_no_store();
    assert(ps_reader_file_favorite("/sdcard/books/checkpoint.epub",true));checkpoint_idle_no_store();
    ps_book_anchor_t a=records_test_anchor(401);assert(ps_reader_real_highlight(&a));checkpoint_idle_no_store();
    uint32_t count=rr->count,sequence=records_sequence;uint64_t index=bank_hash(blobs,"reader-r3-index");
    fail_store_key="reader-r3-index";a.start+=10;a.end+=10;
    assert(!ps_reader_real_highlight(&a)&&rr->count==count&&records_sequence==sequence&&bank_hash(blobs,"reader-r3-index")==index);
    selected_record=0;record_delete();assert(rr->count==count&&records_sequence==sequence&&bank_hash(blobs,"reader-r3-index")==index);
    fail_store_key=NULL;assert(!reader_checkpoint_clean&&ps_reader_power_save());checkpoint_idle_no_store();
    fail_store_key="reader-favs-v1";
    assert(!ps_reader_file_favorite("/sdcard/books/checkpoint.epub",true)&&ps_reader_file_favorite("/sdcard/books/checkpoint.epub",false));
    assert(!ps_reader_file_favorite("/sdcard/books/failed.epub",true)&&!ps_reader_file_favorite("/sdcard/books/failed.epub",false));
    fail_store_key=NULL;assert(!reader_checkpoint_clean&&ps_reader_power_save());checkpoint_idle_no_store();

    // 偏好失败不回滚RAM，必须失效并重试；坏检查点不能因后续成功项而转为干净。
    // Preference failure keeps changed RAM, so it invalidates and retries; later successful fields cannot bless a failed checkpoint.
    fail_store_key="reader-prefs-v1";rp.list=(uint8_t)((rp.list+1)%SHELF_VIEW_COUNT);
    assert(!prefs_save()&&!reader_checkpoint_clean&&!ps_reader_power_save()&&!reader_checkpoint_clean);
    fail_store_key=NULL;assert(ps_reader_power_save()&&reader_checkpoint_clean);reader_prefs_t saved;
    assert(ps_load("reader-prefs-v1",&saved,sizeof(saved))&&saved.list==rp.list);checkpoint_idle_no_store();

    // 新建空草稿也改变anchor，不可沿用上次检查点。 / Even a new blank draft changes its anchor and must invalidate the old checkpoint.
    ps_reader_real_note(&a);assert(!reader_checkpoint_clean&&!editor_dirty);
    assert(editor_append("锁屏前未提交的批注"));fail_store_key="reader-real-d2";
    assert(!ps_reader_power_save()&&!reader_checkpoint_clean&&editor_dirty&&!strcmp(editor_text,"锁屏前未提交的批注"));
    fail_store_key=NULL;assert(ps_reader_power_save()&&reader_checkpoint_clean&&!editor_dirty);
    reader_real_draft_t saved_draft;assert(ps_load("reader-real-d2",&saved_draft,sizeof(saved_draft))&&!strcmp(saved_draft.text,editor_text));
    checkpoint_idle_no_store();

    // 离开失败草稿后其它编辑页会清editor_dirty，检查点仍必须记住RAM草稿尚未提交。
    // Another editor clears editor_dirty after leaving a failed draft; the checkpoint must still remember the unsaved RAM snapshot.
    assert(editor_append("，离页重试"));fail_store_key="reader-real-d2";assert(!draft_save()&&!reader_checkpoint_clean);
    ps_back();ps_edit("无关输入","",32,NULL);assert(!editor_note&&!editor_dirty&&!reader_checkpoint_clean);
    fail_store_key=NULL;assert(ps_reader_power_save()&&ps_load("reader-real-d2",&saved_draft,sizeof(saved_draft))&&strstr(saved_draft.text,"离页重试"));
    ps_back();ps_reader_real_note(&a);assert(!strcmp(editor_text,saved_draft.text));checkpoint_idle_no_store();

    // 正式记录成功而清草稿失败，锁屏仍重试清理；重开不得出现第二条草稿笔记。
    // A committed note with failed draft cleanup is retried on lock and must not restore a second note.
    fail_store_key="reader-real-d2";note_done(editor_text);
    assert(real_draft_clear_pending&&!editor_dirty&&!editor_note);
    assert(!ps_reader_power_save()&&real_draft_clear_pending&&!reader_checkpoint_clean);
    fail_store_key=NULL;assert(ps_reader_power_save()&&!real_draft_clear_pending&&reader_checkpoint_clean);checkpoint_idle_no_store();
    ps_reader_open(0);new_note();assert(!reader_checkpoint_clean&&!editor_dirty);assert(ps_reader_power_save());ps_back();checkpoint_idle_no_store();

    // 只读gate涵盖维护/USB/系统拒绝；显式失效后同卡文件替换不能命中快路径。
    // The pure gate covers maintenance/USB/system refusal; explicit invalidation prevents a same-card replacement from hitting the fast path.
    for(int gate=0;gate<3;gate++){
        storage_held=gate==0;usb_held=gate==1;storage_save_blocked=gate==2;before=stores;
        assert(!ps_reader_power_save()&&stores==before);
    }
    storage_held=usb_held=storage_save_blocked=false;
    reader_prefs_t external=rp;external.list=(uint8_t)((rp.list+1)%SHELF_VIEW_COUNT);assert(blob_store(blobs,"reader-prefs-v1",&external,sizeof(external)));
    ps_reader_checkpoint_invalidate();before=stores;assert(!reader_checkpoint_clean&&ps_reader_power_save()&&stores==before+5);
    assert(ps_load("reader-prefs-v1",&saved,sizeof(saved))&&saved.list==rp.list);checkpoint_idle_no_store();
    ps_reader_media_lost();assert(!reader_checkpoint_clean);before=stores;assert(ps_reader_power_save()&&stores==before+5);checkpoint_idle_no_store();

    // 初始化即使读到有效主档也不授予已提交资格；新目标的显式保存始终重写记录对象。
    // Init does not trust a successful load as a committed checkpoint; explicit new-target saves always rewrite record objects.
    reload_reader_no_writes();assert(!reader_checkpoint_clean);before=stores;assert(ps_reader_power_save()&&stores==before+5);checkpoint_idle_no_store();
    uint32_t next=records_next_id;before=stores;assert(ps_reader_storage_save()&&stores>before+5&&records_next_id>next);checkpoint_idle_no_store();

    // 模拟原子写已把主档移到备份才报错：RAM回滚仍须让下一检查点修复主档。
    // Simulate failure after the primary was moved to backup: even rolled-back RAM must repair the primary on its next checkpoint.
    snprintf(mock_fs_roots[0],sizeof(mock_fs_roots[0]),"%s/checkpoint",rootdir);assert(!mkdir(mock_fs_roots[0],0700));mock_filesystem=true;
    assert(ps_reader_storage_save()&&reader_checkpoint_clean);
    for(int stage=0;stage<3;stage++){
        const char *key=stage==0?"reader-favs-v1":stage==1?"reader-r3-index":NULL;char object[40];
        if(!key){snprintf(object,sizeof(object),"reader-r3-o-%u",records_next_id);key=object;}
        char primary[160],backup[160];snprintf(primary,sizeof(primary),"%s/%s.dat",mock_fs_roots[0],key);snprintf(backup,sizeof(backup),"%s/%s.bak",mock_fs_roots[0],key);
        uint64_t backup_hash=0;char prefix[56];snprintf(prefix,sizeof(prefix),"%s.bak",key);
        if(stage<2){assert(!rename(primary,backup));backup_hash=mock_fs_hash(mock_fs_roots[0],prefix);}
        fail_store_key=key;count=rr->count;sequence=records_sequence;
        if(!stage)assert(!ps_reader_file_favorite("/sdcard/books/checkpoint.epub",true)&&ps_reader_file_favorite("/sdcard/books/checkpoint.epub",false));
        else{a.start+=10;a.end+=10;assert(!ps_reader_real_highlight(&a)&&rr->count==count&&records_sequence==sequence);}
        assert(!reader_checkpoint_clean);fail_store_key=NULL;before=stores;assert(ps_reader_power_save()&&stores>=before+5&&reader_checkpoint_clean);
        if(stage<2){struct stat st;assert(!stat(primary,&st)&&st.st_size>12&&mock_fs_hash(mock_fs_roots[0],prefix)==backup_hash);}
        before=stores;assert(ps_reader_power_save()&&stores==before);
    }
    mock_fs_clear(mock_fs_roots[0]);mock_filesystem=false;
    printf("PASS: reader power checkpoint skips 5 store calls / 69564 unchanged bytes including 8208-byte sequence index; immediate preference/favorite/record saves, failed preference/draft/cleanup retry, record/favorite rollback, blank anchors, pure gate, external/media/init invalidation and forced new-target rewrite.\n");
    records_test_reset();
}
