/*
 * Copyright (c) 2026 Nitin Chauhan / EdgeBridge-S3
 * SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-EdgeBridge-Commercial
 */

#ifndef EB_CAN_H
#define EB_CAN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EB_CAN_FRAME_LOG_CAP 16U

typedef enum {
    EB_CAN_STATE_ERROR_ACTIVE = 0,
    EB_CAN_STATE_ERROR_WARNING = 1,
    EB_CAN_STATE_ERROR_PASSIVE = 2,
    EB_CAN_STATE_BUS_OFF = 3
} eb_can_bus_state_t;

typedef struct {
    uint32_t id;
    uint8_t dlc;
    bool is_extended;
    uint8_t data[8];
    uint64_t timestamp_ms;
} eb_can_frame_t;

typedef struct {
    uint32_t bitrate;
    eb_can_bus_state_t state;
    uint8_t tec; /* Transmit Error Counter */
    uint8_t rec; /* Receive Error Counter */
    uint32_t tx_frames;
    uint32_t rx_frames;
    uint32_t ack_ok_count;
    uint32_t ack_missing_count;
    uint32_t bus_off_count;
    uint32_t recovery_attempts;
    uint64_t last_recovery_ms;
    bool peer_ack_healthy;
    eb_can_frame_t recent_frames[EB_CAN_FRAME_LOG_CAP];
    size_t recent_head;
    size_t recent_count;
} eb_can_diag_t;

void eb_can_init(eb_can_diag_t *diag, uint32_t bitrate);
void eb_can_record_tx_result(eb_can_diag_t *diag, bool ack_received);
void eb_can_record_rx(eb_can_diag_t *diag, const eb_can_frame_t *frame);
void eb_can_update_error_counters(eb_can_diag_t *diag, uint8_t tec, uint8_t rec, bool bus_off);

/* Exponential backoff check for automatic TWAI bus-off recovery. */
bool eb_can_should_attempt_recovery(eb_can_diag_t *diag, uint64_t now_ms);

const char *eb_can_state_str(eb_can_bus_state_t state);
const char *eb_can_health_hint(const eb_can_diag_t *diag);

#ifdef __cplusplus
}
#endif

#endif /* EB_CAN_H */
