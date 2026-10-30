/* SPDX-License-Identifier: Apache-2.0
 * 中文：阅读记录容量与显式、保留源库的合并契约。 / Reader record limits and explicit source-preserving merge contract.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define PS_READER_RECORD_CAPACITY 1024u
#define PS_READER_NOTE_MAX_BYTES 2047u

typedef enum {
    PS_READER_IMPORT_OK,
    PS_READER_IMPORT_NO_SOURCE,
    PS_READER_IMPORT_UNAVAILABLE,
    PS_READER_IMPORT_CORRUPT,
    PS_READER_IMPORT_LIMIT,
    PS_READER_IMPORT_MEMORY,
    PS_READER_IMPORT_SAVE_FAILED,
    PS_READER_IMPORT_BUSY
} ps_reader_records_import_status_t;

typedef struct {
    ps_reader_records_import_status_t status; ///< 只有OK表示目标库已提交。 / Only OK means the target has committed.
    uint32_t source_count, target_count, added, duplicates, total_count;
} ps_reader_records_import_result_t;

/// 显式合并另一个记录根到当前库；不写源库，失败保留原目标及编辑会话。
/// Explicitly merge another record root into the current library; never write the source, preserving the target/editor on failure.
bool ps_reader_records_import(const char *source_root, ps_reader_records_import_result_t *result);

/// 将一个已提交记录库只读转为v3快照；空根导出空索引。/ Read a committed library into a v3 snapshot; an absent root exports an empty index.
bool ps_reader_records_export(const char *source_root,const char *target_root,uint32_t sequence,uint32_t *index_crc);
