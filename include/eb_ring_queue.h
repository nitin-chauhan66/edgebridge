/*
 * Copyright (c) 2026 Nitin Chauhan / EdgeBridge-S3
 * SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-EdgeBridge-Commercial
 */

#ifndef EB_RING_QUEUE_H
#define EB_RING_QUEUE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EB_TELEMETRY_MAX_PAYLOAD 64U

typedef enum {
    EB_BUS_RS485 = 1,
    EB_BUS_CAN   = 2
} eb_bus_type_t;

typedef struct {
    uint64_t timestamp_ms;
    uint32_t seq;
    uint8_t bus;        /* eb_bus_type_t */
    uint8_t addr;       /* Modbus slave ID or CAN node */
    uint16_t len;
    uint32_t can_id;
    uint8_t payload[EB_TELEMETRY_MAX_PAYLOAD];
    uint32_t crc32;
} eb_telemetry_record_t;

/* Lock-free Single-Producer Single-Consumer (SPSC) ring queue */
typedef struct {
    eb_telemetry_record_t *buf;
    size_t capacity;    /* Must be power of 2 */
    volatile size_t head;
    volatile size_t tail;
    uint32_t dropped_count;
} eb_ring_queue_t;

int eb_ring_queue_init(eb_ring_queue_t *q, eb_telemetry_record_t *backing_buf, size_t capacity_pow2);
bool eb_ring_queue_push(eb_ring_queue_t *q, const eb_telemetry_record_t *rec);
bool eb_ring_queue_pop(eb_ring_queue_t *q, eb_telemetry_record_t *out_rec);
size_t eb_ring_queue_count(const eb_ring_queue_t *q);

#ifdef __cplusplus
}
#endif

#endif /* EB_RING_QUEUE_H */
