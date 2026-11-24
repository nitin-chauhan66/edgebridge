/*
 * Copyright (c) 2026 Nitin Chauhan / EdgeBridge-S3
 * SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-EdgeBridge-Commercial
 */

#include "eb_ring_queue.h"
#include <string.h>

static bool is_power_of_two(size_t n) {
    return (n != 0U) && ((n & (n - 1U)) == 0U);
}

int eb_ring_queue_init(eb_ring_queue_t *q, eb_telemetry_record_t *backing_buf, size_t capacity_pow2) {
    if (!q || !backing_buf || !is_power_of_two(capacity_pow2)) {
        return -1;
    }
    q->buf = backing_buf;
    q->capacity = capacity_pow2;
    q->head = 0U;
    q->tail = 0U;
    q->dropped_count = 0U;
    return 0;
}

bool eb_ring_queue_push(eb_ring_queue_t *q, const eb_telemetry_record_t *rec) {
    if (!q || !rec || !q->buf) {
        return false;
    }
    size_t next_head = (q->head + 1U) & (q->capacity - 1U);
    if (next_head == q->tail) {
        q->dropped_count++;
        return false;
    }
    q->buf[q->head] = *rec;
    q->head = next_head;
    return true;
}

bool eb_ring_queue_pop(eb_ring_queue_t *q, eb_telemetry_record_t *out_rec) {
    if (!q || !out_rec || !q->buf) {
        return false;
    }
    if (q->head == q->tail) {
        return false;
    }
    *out_rec = q->buf[q->tail];
    q->tail = (q->tail + 1U) & (q->capacity - 1U);
    return true;
}

size_t eb_ring_queue_count(const eb_ring_queue_t *q) {
    if (!q) {
        return 0U;
    }
    return (q->head - q->tail) & (q->capacity - 1U);
}
