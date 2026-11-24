/*
 * Copyright (c) 2026 Nitin Chauhan / EdgeBridge-S3
 * SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-EdgeBridge-Commercial
 */

#ifndef EB_STORAGE_H
#define EB_STORAGE_H

#include "eb_ring_queue.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EB_STORAGE_META_MAGIC 0x45425342U /* "EBSB" */

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t generation;
    uint32_t capacity;
    uint32_t head;
    uint32_t tail;
    uint32_t count;
    uint32_t next_seq;
    uint32_t overwritten_count;
    uint32_t corrupt_skipped_count;
    uint32_t crc32;
} eb_storage_superblock_t;

/*
 * Persistence backend vtable for circular store-and-forward storage.
 * On Zephyr ESP32-S3: backed by LittleFS files under /telemetry_fs/.
 * On Host/Tests: backed by POSIX files (with optional power-cut fault injection) or RAM.
 */
typedef struct {
    int (*read_meta)(void *ctx, uint8_t bank, eb_storage_superblock_t *out_sb);
    int (*write_meta)(void *ctx, uint8_t bank, const eb_storage_superblock_t *sb);
    int (*read_slot)(void *ctx, size_t idx, eb_telemetry_record_t *out_rec);
    int (*write_slot)(void *ctx, size_t idx, const eb_telemetry_record_t *rec);
    int (*sync)(void *ctx);
    void *ctx;
} eb_storage_backend_t;

typedef struct {
    eb_telemetry_record_t *slots;
    size_t capacity;
    size_t head;      /* Next write index */
    size_t tail;      /* Oldest unread index */
    size_t count;
    uint32_t next_seq;
    uint32_t overwritten_count;
    uint32_t corrupt_skipped_count;
    uint32_t meta_generation;
    uint32_t writes_since_meta_sync;
    const eb_storage_backend_t *backend;
} eb_storage_ring_t;

int eb_storage_init(eb_storage_ring_t *ring, eb_telemetry_record_t *backing_slots, size_t capacity);

/* Attach a persistent backend (LittleFS or host file) and recover/replay state. */
int eb_storage_open_persistent(eb_storage_ring_t *ring,
                               const eb_storage_backend_t *backend,
                               size_t capacity);

/* Persist alternating superblock checkpoint to flash/disk. */
int eb_storage_sync_meta(eb_storage_ring_t *ring);

uint32_t eb_storage_record_crc32(const eb_telemetry_record_t *rec);

/* Append record; overwrites oldest if full (circular store-and-forward). */
int eb_storage_append(eb_storage_ring_t *ring, eb_telemetry_record_t *rec);

/* Peek oldest valid record (FIFO drain); skips any corrupt/half-written slots. */
bool eb_storage_peek_oldest(eb_storage_ring_t *ring, eb_telemetry_record_t *out_rec);

/* Acknowledge oldest record after successful MQTT/HTTP publish. */
bool eb_storage_ack_oldest(eb_storage_ring_t *ring);

/* Simulate a power-cut mid-write (marks oldest slot with bad CRC to test recovery). */
void eb_storage_inject_corrupt_tail_for_test(eb_storage_ring_t *ring);

#ifndef __ZEPHYR__
typedef struct {
    char meta_path[256];
    char data_path[256];
    bool inject_torn_next_write;
    bool inject_torn_next_meta;
} eb_posix_storage_ctx_t;

int eb_storage_posix_backend_init(eb_posix_storage_ctx_t *pctx,
                                  eb_storage_backend_t *out_backend,
                                  const char *meta_path,
                                  const char *data_path);
#endif

#ifdef __cplusplus
}
#endif

#endif /* EB_STORAGE_H */
