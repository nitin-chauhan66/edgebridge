#include "eb_can.h"
#include <string.h>

void eb_can_init(eb_can_diag_t *diag, uint32_t bitrate) {
    if (!diag) {
        return;
    }
    memset(diag, 0, sizeof(*diag));
    diag->bitrate = (bitrate > 0U) ? bitrate : 500000U;
    diag->state = EB_CAN_STATE_ERROR_ACTIVE;
    diag->peer_ack_healthy = false;
}

void eb_can_record_tx_result(eb_can_diag_t *diag, bool ack_received) {
    if (!diag) {
        return;
    }
    diag->tx_frames++;
    if (ack_received) {
        diag->ack_ok_count++;
        diag->peer_ack_healthy = true;
    } else {
        diag->ack_missing_count++;
        diag->peer_ack_healthy = false;
    }
}

void eb_can_record_rx(eb_can_diag_t *diag, const eb_can_frame_t *frame) {
    if (!diag || !frame) {
        return;
    }
    diag->rx_frames++;
    diag->peer_ack_healthy = true;
    diag->recent_frames[diag->recent_head] = *frame;
    diag->recent_head = (diag->recent_head + 1U) % EB_CAN_FRAME_LOG_CAP;
    if (diag->recent_count < EB_CAN_FRAME_LOG_CAP) {
        diag->recent_count++;
    }
}

void eb_can_update_error_counters(eb_can_diag_t *diag, uint8_t tec, uint8_t rec, bool bus_off) {
    if (!diag) {
        return;
    }
    diag->tec = tec;
    diag->rec = rec;
    if (bus_off) {
        if (diag->state != EB_CAN_STATE_BUS_OFF) {
            diag->bus_off_count++;
        }
        diag->state = EB_CAN_STATE_BUS_OFF;
        diag->peer_ack_healthy = false;
    } else if (tec >= 128U || rec >= 128U) {
        diag->state = EB_CAN_STATE_ERROR_PASSIVE;
        if (tec >= 128U && diag->rx_frames == 0U && diag->ack_ok_count == 0U) {
            diag->peer_ack_healthy = false;
        }
    } else if (tec >= 96U || rec >= 96U) {
        diag->state = EB_CAN_STATE_ERROR_WARNING;
    } else {
        diag->state = EB_CAN_STATE_ERROR_ACTIVE;
        diag->recovery_attempts = 0U;
    }
}

bool eb_can_should_attempt_recovery(eb_can_diag_t *diag, uint64_t now_ms) {
    if (!diag || diag->state != EB_CAN_STATE_BUS_OFF) {
        return false;
    }
    uint32_t shift = diag->recovery_attempts < 6U ? diag->recovery_attempts : 6U;
    uint64_t backoff_ms = 100ULL << shift;
    if (backoff_ms > 5000ULL) {
        backoff_ms = 5000ULL;
    }
    if (diag->last_recovery_ms == 0ULL || (now_ms - diag->last_recovery_ms) >= backoff_ms) {
        diag->last_recovery_ms = now_ms;
        diag->recovery_attempts++;
        return true;
    }
    return false;
}

const char *eb_can_state_str(eb_can_bus_state_t state) {
    switch (state) {
        case EB_CAN_STATE_ERROR_ACTIVE:  return "ERROR_ACTIVE";
        case EB_CAN_STATE_ERROR_WARNING: return "ERROR_WARNING";
        case EB_CAN_STATE_ERROR_PASSIVE: return "ERROR_PASSIVE";
        case EB_CAN_STATE_BUS_OFF:       return "BUS_OFF";
        default:                         return "UNKNOWN";
    }
}

const char *eb_can_health_hint(const eb_can_diag_t *diag) {
    if (!diag) {
        return "Uninitialized";
    }
    if (diag->state == EB_CAN_STATE_BUS_OFF) {
        return "BUS_OFF: Check shorted CANH/CANL, 120R termination, or bitrate mismatch";
    }
    if (diag->state == EB_CAN_STATE_ERROR_PASSIVE && !diag->peer_ack_healthy) {
        return "No peer ACK (Error Passive): Check inverter/BMS power, CANH/CANL polarity, and 120R jumper";
    }
    if (diag->ack_missing_count > 0U && !diag->peer_ack_healthy) {
        return "TX unacknowledged: Peer node silent or missing 120R termination";
    }
    if (diag->rx_frames == 0U && diag->tx_frames == 0U) {
        return "Idle / listening for CAN frames";
    }
    return "Healthy: Peer ACKing and active CAN traffic verified";
}
