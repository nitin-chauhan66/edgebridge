#include "eb_storage.h"
#include "eb_crc.h"
#include <stddef.h>
#include <stdio.h>
#include <string.h>

static uint32_t superblock_crc32(const eb_storage_superblock_t *sb) {
    return eb_crc32_ieee(sb, offsetof(eb_storage_superblock_t, crc32));
}

static bool superblock_valid(const eb_storage_superblock_t *sb, size_t expected_cap) {
    if (!sb || sb->magic != EB_STORAGE_META_MAGIC || sb->version != 1U) {
        return false;
    }
    if (sb->capacity != expected_cap || sb->capacity == 0U) {
        return false;
    }
    if (sb->head >= sb->capacity || sb->tail >= sb->capacity || sb->count > sb->capacity) {
        return false;
    }
    return sb->crc32 == superblock_crc32(sb);
}

uint32_t eb_storage_record_crc32(const eb_telemetry_record_t *rec) {
    if (!rec) {
        return 0U;
    }
    return eb_crc32_ieee(rec, offsetof(eb_telemetry_record_t, crc32));
}

static int read_slot(const eb_storage_ring_t *ring, size_t idx, eb_telemetry_record_t *out) {
    if (ring->backend && ring->backend->read_slot) {
        return ring->backend->read_slot(ring->backend->ctx, idx, out);
    }
    if (!ring->slots || idx >= ring->capacity) {
        return -1;
    }
    *out = ring->slots[idx];
    return 0;
}

static int write_slot(const eb_storage_ring_t *ring, size_t idx, const eb_telemetry_record_t *rec) {
    if (ring->backend && ring->backend->write_slot) {
        return ring->backend->write_slot(ring->backend->ctx, idx, rec);
    }
    if (!ring->slots || idx >= ring->capacity) {
        return -1;
    }
    ring->slots[idx] = *rec;
    return 0;
}

int eb_storage_init(eb_storage_ring_t *ring, eb_telemetry_record_t *backing_slots, size_t capacity) {
    if (!ring || !backing_slots || capacity == 0U) {
        return -1;
    }
    memset(ring, 0, sizeof(*ring));
    ring->slots = backing_slots;
    ring->capacity = capacity;
    ring->next_seq = 1U;
    return 0;
}

int eb_storage_sync_meta(eb_storage_ring_t *ring) {
    if (!ring || !ring->backend || !ring->backend->write_meta) {
        return 0;
    }
    ring->meta_generation++;
    eb_storage_superblock_t sb;
    memset(&sb, 0, sizeof(sb));
    sb.magic = EB_STORAGE_META_MAGIC;
    sb.version = 1U;
    sb.generation = ring->meta_generation;
    sb.capacity = (uint32_t)ring->capacity;
    sb.head = (uint32_t)ring->head;
    sb.tail = (uint32_t)ring->tail;
    sb.count = (uint32_t)ring->count;
    sb.next_seq = ring->next_seq;
    sb.overwritten_count = ring->overwritten_count;
    sb.corrupt_skipped_count = ring->corrupt_skipped_count;
    sb.crc32 = superblock_crc32(&sb);

    uint8_t bank = (uint8_t)(ring->meta_generation & 1U);
    int rc = ring->backend->write_meta(ring->backend->ctx, bank, &sb);
    if (rc == 0 && ring->backend->sync) {
        rc = ring->backend->sync(ring->backend->ctx);
    }
    if (rc == 0) {
        ring->writes_since_meta_sync = 0U;
    }
    return rc;
}

int eb_storage_open_persistent(eb_storage_ring_t *ring,
                               const eb_storage_backend_t *backend,
                               size_t capacity) {
    if (!ring || !backend || !backend->read_meta || !backend->write_meta ||
        !backend->read_slot || !backend->write_slot || capacity == 0U) {
        return -1;
    }
    memset(ring, 0, sizeof(*ring));
    ring->backend = backend;
    ring->capacity = capacity;
    ring->next_seq = 1U;

    eb_storage_superblock_t sb[2];
    bool ok0 = (backend->read_meta(backend->ctx, 0U, &sb[0]) == 0) && superblock_valid(&sb[0], capacity);
    bool ok1 = (backend->read_meta(backend->ctx, 1U, &sb[1]) == 0) && superblock_valid(&sb[1], capacity);

    if (ok0 || ok1) {
        const eb_storage_superblock_t *best = NULL;
        if (ok0 && ok1) {
            best = (sb[0].generation >= sb[1].generation) ? &sb[0] : &sb[1];
        } else {
            best = ok0 ? &sb[0] : &sb[1];
        }
        ring->meta_generation = best->generation;
        ring->head = best->head;
        ring->tail = best->tail;
        ring->count = best->count;
        ring->next_seq = best->next_seq;
        ring->overwritten_count = best->overwritten_count;
        ring->corrupt_skipped_count = best->corrupt_skipped_count;

        /* Replay any valid-CRC slots appended after the last superblock checkpoint. */
        for (size_t step = 0U; step < capacity; step++) {
            eb_telemetry_record_t probe;
            if (read_slot(ring, ring->head, &probe) != 0) {
                break;
            }
            if (probe.crc32 != eb_storage_record_crc32(&probe) ||
                probe.seq < ring->next_seq) {
                break;
            }
            if (ring->count == ring->capacity) {
                ring->tail = (ring->tail + 1U) % ring->capacity;
                ring->count--;
                ring->overwritten_count++;
            }
            ring->head = (ring->head + 1U) % ring->capacity;
            ring->count++;
            ring->next_seq = probe.seq + 1U;
        }
        return 0;
    }

    /* Fresh initialization of both superblocks */
    int rc = eb_storage_sync_meta(ring);
    if (rc == 0) {
        rc = eb_storage_sync_meta(ring);
    }
    return rc;
}

int eb_storage_append(eb_storage_ring_t *ring, eb_telemetry_record_t *rec) {
    if (!ring || !rec || (!ring->slots && !ring->backend)) {
        return -1;
    }
    eb_telemetry_record_t tmp = *rec;
    tmp.seq = ring->next_seq;
    tmp.crc32 = eb_storage_record_crc32(&tmp);

    bool wrapped = (ring->count == ring->capacity);
    if (write_slot(ring, ring->head, &tmp) != 0) {
        return -1;
    }

    *rec = tmp;
    ring->next_seq++;
    if (wrapped) {
        /* Circular overwrite of oldest slot */
        ring->tail = (ring->tail + 1U) % ring->capacity;
        ring->count--;
        ring->overwritten_count++;
    }
    ring->head = (ring->head + 1U) % ring->capacity;
    ring->count++;
    ring->writes_since_meta_sync++;

    if (ring->backend && (wrapped || ring->writes_since_meta_sync >= 4U)) {
        (void)eb_storage_sync_meta(ring);
    }
    return 0;
}

bool eb_storage_peek_oldest(eb_storage_ring_t *ring, eb_telemetry_record_t *out_rec) {
    if (!ring || !out_rec || (!ring->slots && !ring->backend)) {
        return false;
    }
    bool skipped = false;
    while (ring->count > 0U) {
        eb_telemetry_record_t slot;
        if (read_slot(ring, ring->tail, &slot) == 0 &&
            slot.crc32 == eb_storage_record_crc32(&slot)) {
            if (skipped && ring->backend) {
                (void)eb_storage_sync_meta(ring);
            }
            *out_rec = slot;
            return true;
        }
        /* Power-cut mid-write detected: skip corrupt slot cleanly */
        ring->tail = (ring->tail + 1U) % ring->capacity;
        ring->count--;
        ring->corrupt_skipped_count++;
        skipped = true;
    }
    if (skipped && ring->backend) {
        (void)eb_storage_sync_meta(ring);
    }
    return false;
}

bool eb_storage_ack_oldest(eb_storage_ring_t *ring) {
    if (!ring || ring->count == 0U) {
        return false;
    }
    ring->tail = (ring->tail + 1U) % ring->capacity;
    ring->count--;
    if (ring->backend) {
        (void)eb_storage_sync_meta(ring);
    }
    return true;
}

void eb_storage_inject_corrupt_tail_for_test(eb_storage_ring_t *ring) {
    if (!ring || ring->count == 0U) {
        return;
    }
    if (ring->backend) {
        eb_telemetry_record_t slot;
        if (read_slot(ring, ring->tail, &slot) == 0) {
            slot.crc32 ^= 0xDEADBEEFU;
            (void)write_slot(ring, ring->tail, &slot);
        }
    } else if (ring->slots) {
        ring->slots[ring->tail].crc32 ^= 0xDEADBEEFU;
    }
}

#ifndef __ZEPHYR__
static int posix_read_meta(void *ctx, uint8_t bank, eb_storage_superblock_t *out_sb) {
    eb_posix_storage_ctx_t *p = (eb_posix_storage_ctx_t *)ctx;
    FILE *f = fopen(p->meta_path, "rb");
    if (!f) {
        return -1;
    }
    long offset = (long)bank * (long)sizeof(eb_storage_superblock_t);
    int rc = -1;
    if (fseek(f, offset, SEEK_SET) == 0 &&
        fread(out_sb, sizeof(eb_storage_superblock_t), 1U, f) == 1U) {
        rc = 0;
    }
    fclose(f);
    return rc;
}

static int posix_write_meta(void *ctx, uint8_t bank, const eb_storage_superblock_t *sb) {
    eb_posix_storage_ctx_t *p = (eb_posix_storage_ctx_t *)ctx;
    FILE *f = fopen(p->meta_path, "r+b");
    if (!f) {
        f = fopen(p->meta_path, "w+b");
    }
    if (!f) {
        return -1;
    }
    long offset = (long)bank * (long)sizeof(eb_storage_superblock_t);
    int rc = -1;
    if (fseek(f, offset, SEEK_SET) == 0) {
        if (p->inject_torn_next_meta) {
            p->inject_torn_next_meta = false;
            /* Simulate a power cut halfway through updating one superblock bank */
            size_t half = sizeof(eb_storage_superblock_t) / 2U;
            uint8_t corrupt[sizeof(eb_storage_superblock_t)];
            memcpy(corrupt, sb, sizeof(corrupt));
            corrupt[half - 1U] ^= 0xFFU;
            (void)fwrite(corrupt, half, 1U, f);
            fflush(f);
            fclose(f);
            return -2;
        }
        if (fwrite(sb, sizeof(eb_storage_superblock_t), 1U, f) == 1U) {
            fflush(f);
            rc = 0;
        }
    }
    fclose(f);
    return rc;
}

static int posix_read_slot(void *ctx, size_t idx, eb_telemetry_record_t *out_rec) {
    eb_posix_storage_ctx_t *p = (eb_posix_storage_ctx_t *)ctx;
    FILE *f = fopen(p->data_path, "rb");
    if (!f) {
        return -1;
    }
    long offset = (long)idx * (long)sizeof(eb_telemetry_record_t);
    int rc = -1;
    if (fseek(f, offset, SEEK_SET) == 0 &&
        fread(out_rec, sizeof(eb_telemetry_record_t), 1U, f) == 1U) {
        rc = 0;
    }
    fclose(f);
    return rc;
}

static int posix_write_slot(void *ctx, size_t idx, const eb_telemetry_record_t *rec) {
    eb_posix_storage_ctx_t *p = (eb_posix_storage_ctx_t *)ctx;
    FILE *f = fopen(p->data_path, "r+b");
    if (!f) {
        f = fopen(p->data_path, "w+b");
    }
    if (!f) {
        return -1;
    }
    long offset = (long)idx * (long)sizeof(eb_telemetry_record_t);
    int rc = -1;
    if (fseek(f, offset, SEEK_SET) == 0) {
        if (p->inject_torn_next_write) {
            p->inject_torn_next_write = false;
            /* Simulate power cut mid-record write: corrupt tail bytes / CRC */
            eb_telemetry_record_t torn = *rec;
            torn.crc32 ^= 0xA5A55A5AU;
            (void)fwrite(&torn, sizeof(torn), 1U, f);
            fflush(f);
            fclose(f);
            return -2;
        }
        if (fwrite(rec, sizeof(eb_telemetry_record_t), 1U, f) == 1U) {
            fflush(f);
            rc = 0;
        }
    }
    fclose(f);
    return rc;
}

static int posix_sync(void *ctx) {
    (void)ctx;
    return 0;
}

int eb_storage_posix_backend_init(eb_posix_storage_ctx_t *pctx,
                                  eb_storage_backend_t *out_backend,
                                  const char *meta_path,
                                  const char *data_path) {
    if (!pctx || !out_backend || !meta_path || !data_path) {
        return -1;
    }
    memset(pctx, 0, sizeof(*pctx));
    snprintf(pctx->meta_path, sizeof(pctx->meta_path), "%s", meta_path);
    snprintf(pctx->data_path, sizeof(pctx->data_path), "%s", data_path);

    out_backend->read_meta = posix_read_meta;
    out_backend->write_meta = posix_write_meta;
    out_backend->read_slot = posix_read_slot;
    out_backend->write_slot = posix_write_slot;
    out_backend->sync = posix_sync;
    out_backend->ctx = pctx;
    return 0;
}
#endif
