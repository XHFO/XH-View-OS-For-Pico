/* SPDX-License-Identifier: Apache-2.0
 * 中文：生产快照序列化的只读源、兼容与失败注入主机检查。
 * English: Host checks for read-only sources, compatibility and failure injection in the production snapshot serializer.
 */
static void test_reader_snapshot_export(void){
    records_test_reset();char target[120];snprintf(target,sizeof(target),"%s/snapshot",rootdir);assert(!mkdir(target,0700));
    make_v3_fixture(source_blobs,1024,1);uint64_t original=bank_hash(source_blobs,"");uint32_t sequence=records_sequence,next=records_next_id,crc=0;
    unsigned original_count=rr->count;max_record_allocation=0;snapshot_writes=0;
    assert(ps_reader_records_export("/mock/source",target,11,&crc));
    reader_index_v3_t *index=calloc(1,sizeof(*index));assert(index&&mock_fs_load(target,"reader-r3-index",index,sizeof(*index)));
    assert(index->count==1024&&index->next_id==1025&&index->sequence==11&&crc==mock_fs_crc(index,sizeof(*index))&&snapshot_writes==1025);
    assert(bank_hash(source_blobs,"")==original&&rr->count==original_count&&records_sequence==sequence&&records_next_id==next);
    assert(max_record_allocation<=sizeof(reader_index_v3_t));
    reader_object_v3_t *object=calloc(1,index->object[0].bytes);assert(object&&mock_fs_load(target,"reader-r3-o-1",object,index->object[0].bytes)&&object->version==3);free(object);
    assert(!ps_reader_records_export("/mock/source",target,0,&crc));
    blobs_clear(source_blobs);make_v2_fixture(source_blobs,false);original=bank_hash(source_blobs,"");snapshot_writes=0;
    assert(ps_reader_records_export("/mock/source",target,12,&crc)&&snapshot_writes==71);
    assert(mock_fs_load(target,"reader-r3-index",index,sizeof(*index))&&index->count==70&&index->sequence==12&&bank_hash(source_blobs,"")==original);
    unsigned char *saved=malloc(sizeof(*index));assert(saved);memcpy(saved,index,sizeof(*index));
    snapshot_fail_key="reader-r3-o-4";assert(!ps_reader_records_export("/mock/source",target,13,&crc));snapshot_fail_key=NULL;
    assert(mock_fs_load(target,"reader-r3-index",index,sizeof(*index))&&!memcmp(saved,index,sizeof(*index)));free(saved);
    assert(ps_reader_records_export(NULL,target,14,&crc)&&mock_fs_load(target,"reader-r3-index",index,sizeof(*index))&&index->count==0&&index->next_id==1);
    allocation_fail_after=0;assert(!ps_reader_records_export("/mock/source",target,15,&crc));allocation_fail_after=-1;
    free(index);mock_fs_clear(target);records_test_reset();
    puts("PASS: production v3/v2 streaming reading snapshot export, 1024 records, bounded allocation, unchanged sources/live IDs, empty source and failures.");
}
