#include "eb_scanner.h"
#include <string.h>

static const uint32_t SCAN_BAUDS[] = {9600U, 19200U, 38400U, 57600U, 115200U};
static const uint16_t SCAN_PROBE_REGS[] = {0x1200U, 0x0000U, 0x0001U};
static const uint32_t SCAN_CAN_BITRATES[] = {500000U, 250000U, 125000U, 1000000U};

static bool already_found_slave(const eb_scan_result_t *res, uint32_t baud, uint8_t slave_id) {
    for (size_t i = 0U; i < res->hit_count; i++) {
        if (res->hits[i].baud_rate == baud && res->hits[i].slave_id == slave_id) {
            return true;
        }
    }
    return false;
}

int eb_scanner_run_rs485(eb_rs485_ctx_t *rs485,
                         const eb_rs485_transport_t *xp,
                         uint8_t min_slave_id,
                         uint8_t max_slave_id,
                         eb_scan_result_t *out_result) {
    if (!rs485 || !xp || !out_result || min_slave_id == 0U || min_slave_id > max_slave_id) {
        return -1;
    }

    uint32_t orig_baud = rs485->baud_rate;
    uint32_t orig_extra = rs485->guard_extra_us;
    bool orig_auto = rs485->auto_direction;

    memset(out_result, 0, sizeof(*out_result));

    for (size_t bi = 0U; bi < sizeof(SCAN_BAUDS) / sizeof(SCAN_BAUDS[0]); bi++) {
        uint32_t baud = SCAN_BAUDS[bi];
        if (xp->set_baud && xp->set_baud(xp->user_ctx, baud) != 0) {
            continue;
        }
        eb_rs485_init(rs485, baud, orig_extra, orig_auto, true);
        out_result->bauds_probed++;

        for (uint16_t sid = min_slave_id; sid <= max_slave_id; sid++) {
            for (size_t ri = 0U; ri < sizeof(SCAN_PROBE_REGS) / sizeof(SCAN_PROBE_REGS[0]); ri++) {
                if (already_found_slave(out_result, baud, (uint8_t)sid)) {
                    break;
                }
                uint8_t rx_buf[64];
                size_t rx_len = 0U;
                out_result->probes_sent++;
                eb_rs485_status_t st = eb_rs485_transact(
                    rs485, xp, (uint8_t)sid, 0x03U, SCAN_PROBE_REGS[ri], 1U,
                    60U, rx_buf, sizeof(rx_buf), &rx_len);

                if (st == EB_RS485_OK || st == EB_RS485_ERR_EXCEPTION) {
                    if (out_result->hit_count < EB_SCAN_MAX_RS485_HITS) {
                        eb_scan_rs485_hit_t *hit = &out_result->hits[out_result->hit_count++];
                        hit->baud_rate = baud;
                        hit->slave_id = (uint8_t)sid;
                        hit->probe_func = 0x03U;
                        hit->probe_reg = SCAN_PROBE_REGS[ri];
                        hit->is_exception = (st == EB_RS485_ERR_EXCEPTION);
                        hit->exception_code = hit->is_exception ? rs485->last_exception_code : 0U;
                    }
                    break;
                }
            }
        }
    }

    /* Restore original bus configuration if no hits changed it */
    if (xp->set_baud) {
        xp->set_baud(xp->user_ctx, orig_baud);
    }
    eb_rs485_init(rs485, orig_baud, orig_extra, orig_auto, true);
    out_result->scan_completed = true;
    return 0;
}

int eb_scanner_run_can(const eb_can_scan_transport_t *xp,
                       uint32_t window_per_rate_ms,
                       eb_scan_result_t *out_result) {
    if (!xp || !xp->set_listen_bitrate || !xp->listen_frames || !out_result) {
        return -1;
    }
    uint32_t win = (window_per_rate_ms > 0U) ? window_per_rate_ms : 250U;
    out_result->detected_can_bitrate = 0U;
    out_result->can_id_count = 0U;

    for (size_t i = 0U; i < sizeof(SCAN_CAN_BITRATES) / sizeof(SCAN_CAN_BITRATES[0]); i++) {
        uint32_t rate = SCAN_CAN_BITRATES[i];
        if (xp->set_listen_bitrate(xp->user_ctx, rate) != 0) {
            continue;
        }
        size_t seen = 0U;
        if (xp->listen_frames(xp->user_ctx, win,
                              out_result->can_ids, EB_SCAN_MAX_CAN_IDS, &seen) == 0 &&
            seen > 0U) {
            out_result->detected_can_bitrate = rate;
            out_result->can_id_count = seen;
            return 0;
        }
    }
    return 0;
}
