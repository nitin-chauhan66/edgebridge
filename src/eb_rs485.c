#include "eb_rs485.h"
#include <string.h>

static void log_frame(eb_rs485_ctx_t *ctx, uint64_t ts_ms, bool is_tx,
                      uint8_t slave_id, uint8_t func, const uint8_t *buf,
                      size_t len, eb_rs485_status_t status) {
    if (!ctx) {
        return;
    }
    eb_rs485_frame_log_t *slot = &ctx->recent_frames[ctx->recent_head];
    memset(slot, 0, sizeof(*slot));
    slot->timestamp_ms = ts_ms;
    slot->is_tx = is_tx;
    slot->slave_id = slave_id;
    slot->func_code = func;
    slot->len = (uint8_t)(len > 255U ? 255U : len);
    slot->status = (int8_t)status;
    if (buf && len > 0U) {
        size_t copy_n = len < sizeof(slot->preview) ? len : sizeof(slot->preview);
        memcpy(slot->preview, buf, copy_n);
    }
    ctx->recent_head = (ctx->recent_head + 1U) % EB_RS485_FRAME_LOG_CAP;
    if (ctx->recent_count < EB_RS485_FRAME_LOG_CAP) {
        ctx->recent_count++;
    }
}

uint32_t eb_rs485_calc_t35_us(uint32_t baud_rate, uint32_t extra_margin_us) {
    if (baud_rate == 0U) {
        return EB_RS485_MIN_T35_US + extra_margin_us;
    }
    /*
     * Modbus RTU specification:
     * For baud <= 19200: 3.5 character times (11 bits/char -> 38.5 bits)
     * For baud >  19200: fixed minimum 1750 us, plus isolator propagation margin.
     */
    uint32_t t35 = EB_RS485_MIN_T35_US;
    if (baud_rate <= 19200U) {
        t35 = (uint32_t)(((uint64_t)38500000ULL + baud_rate - 1ULL) / baud_rate);
        if (t35 < EB_RS485_MIN_T35_US) {
            t35 = EB_RS485_MIN_T35_US;
        }
    }
    return t35 + extra_margin_us;
}

uint32_t eb_rs485_frame_tx_time_us(uint32_t baud_rate, size_t len) {
    if (baud_rate == 0U || len == 0U) {
        return 0U;
    }
    /* 1 start + 8 data + parity/stop = 11 bit-times per byte */
    return (uint32_t)(((uint64_t)len * 11ULL * 1000000ULL + baud_rate - 1ULL) / baud_rate);
}

void eb_rs485_init(eb_rs485_ctx_t *ctx, uint32_t baud_rate, uint32_t guard_extra_us,
                   bool auto_direction, bool strict_cap) {
    if (!ctx) {
        return;
    }
    memset(ctx, 0, sizeof(*ctx));
    ctx->auto_direction = auto_direction;
    ctx->guard_extra_us = guard_extra_us;

    if (baud_rate > EB_RS485_ISOLATOR_BAUD_CEILING) {
        ctx->baud_ceiling_warn = true;
        ctx->baud_rate = strict_cap ? EB_RS485_ISOLATOR_BAUD_CEILING : baud_rate;
    } else {
        ctx->baud_ceiling_warn = false;
        ctx->baud_rate = (baud_rate >= 1200U) ? baud_rate : 9600U;
    }
    ctx->effective_t35_us = eb_rs485_calc_t35_us(ctx->baud_rate, ctx->guard_extra_us);
}

uint16_t eb_rs485_crc16(const uint8_t *data, size_t len) {
    uint16_t crc = 0xFFFFU;
    if (!data && len > 0U) {
        return crc;
    }
    for (size_t i = 0U; i < len; i++) {
        crc ^= (uint16_t)data[i];
        for (int b = 0; b < 8; b++) {
            if (crc & 0x0001U) {
                crc = (crc >> 1) ^ 0xA001U;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

size_t eb_rs485_build_read_req(uint8_t slave_id, uint8_t func_code,
                               uint16_t start_reg, uint16_t reg_count,
                               uint8_t out_frame[8]) {
    if (!out_frame) {
        return 0U;
    }
    out_frame[0] = slave_id;
    out_frame[1] = func_code;
    out_frame[2] = (uint8_t)((start_reg >> 8) & 0xFFU);
    out_frame[3] = (uint8_t)(start_reg & 0xFFU);
    out_frame[4] = (uint8_t)((reg_count >> 8) & 0xFFU);
    out_frame[5] = (uint8_t)(reg_count & 0xFFU);
    uint16_t crc = eb_rs485_crc16(out_frame, 6U);
    out_frame[6] = (uint8_t)(crc & 0xFFU);
    out_frame[7] = (uint8_t)((crc >> 8) & 0xFFU);
    return 8U;
}

size_t eb_rs485_build_write_single_req(uint8_t slave_id, uint16_t reg_addr,
                                       uint16_t value, uint8_t out_frame[8]) {
    return eb_rs485_build_read_req(slave_id, 0x06U, reg_addr, value, out_frame);
}

size_t eb_rs485_expected_reply_len(uint8_t func_code, uint16_t reg_or_coil_count) {
    switch (func_code) {
        case 0x01U:
        case 0x02U:
            return (size_t)((reg_or_coil_count + 7U) / 8U) + 5U;
        case 0x03U:
        case 0x04U:
            return (size_t)(reg_or_coil_count * 2U) + 5U;
        case 0x06U:
            return 8U;
        default:
            return 5U;
    }
}

static void adapt_guard_on_error(eb_rs485_ctx_t *ctx) {
    ctx->consecutive_bus_errors++;
    if (ctx->consecutive_bus_errors >= 2U &&
        ctx->adaptive_extra_us < EB_RS485_MAX_ADAPTIVE_EXTRA_US) {
        ctx->adaptive_extra_us += 250U;
        if (ctx->adaptive_extra_us > EB_RS485_MAX_ADAPTIVE_EXTRA_US) {
            ctx->adaptive_extra_us = EB_RS485_MAX_ADAPTIVE_EXTRA_US;
        }
        ctx->effective_t35_us = eb_rs485_calc_t35_us(
            ctx->baud_rate, ctx->guard_extra_us + ctx->adaptive_extra_us);
    }
}

eb_rs485_status_t eb_rs485_validate_reply(eb_rs485_ctx_t *ctx,
                                          uint8_t expected_slave,
                                          uint8_t expected_func,
                                          const uint8_t *rx_buf,
                                          size_t rx_len) {
    if (!ctx || (!rx_buf && rx_len > 0U)) {
        return EB_RS485_ERR_INVALID_ARG;
    }
    if (rx_len == 0U) {
        ctx->timeout_errors++;
        return EB_RS485_ERR_TIMEOUT;
    }
    if (rx_len < 5U) {
        ctx->framing_clipped_errors++;
        adapt_guard_on_error(ctx);
        return EB_RS485_ERR_TRUNCATED;
    }
    if (rx_buf[0] != expected_slave) {
        ctx->framing_clipped_errors++;
        adapt_guard_on_error(ctx);
        return EB_RS485_ERR_SLAVE_MISMATCH;
    }

    bool is_exception = ((rx_buf[1] & 0x80U) != 0U) &&
                        ((rx_buf[1] & 0x7FU) == expected_func);

    if (!is_exception) {
        if (rx_buf[1] != expected_func) {
            ctx->framing_clipped_errors++;
            adapt_guard_on_error(ctx);
            return EB_RS485_ERR_TRUNCATED;
        }
        if (expected_func == 0x01U || expected_func == 0x02U ||
            expected_func == 0x03U || expected_func == 0x04U) {
            uint8_t byte_count = rx_buf[2];
            size_t expected_len = (size_t)byte_count + 5U;
            if (rx_len < expected_len) {
                /* Classic auto-direction turnaround clipping */
                ctx->framing_clipped_errors++;
                adapt_guard_on_error(ctx);
                return EB_RS485_ERR_TRUNCATED;
            }
            rx_len = expected_len;
        } else if (expected_func == 0x06U && rx_len < 8U) {
            ctx->framing_clipped_errors++;
            adapt_guard_on_error(ctx);
            return EB_RS485_ERR_TRUNCATED;
        }
    } else {
        rx_len = 5U;
    }

    uint16_t calc_crc = eb_rs485_crc16(rx_buf, rx_len - 2U);
    uint16_t rx_crc = (uint16_t)rx_buf[rx_len - 2U] |
                      ((uint16_t)rx_buf[rx_len - 1U] << 8);
    if (calc_crc != rx_crc) {
        ctx->crc_errors++;
        adapt_guard_on_error(ctx);
        return EB_RS485_ERR_CRC;
    }

    ctx->consecutive_bus_errors = 0U;
    ctx->rx_frames++;
    if (is_exception) {
        ctx->exception_responses++;
        ctx->last_exception_code = rx_buf[2];
        return EB_RS485_ERR_EXCEPTION;
    }
    return EB_RS485_OK;
}

eb_rs485_status_t eb_rs485_transact(eb_rs485_ctx_t *ctx,
                                    const eb_rs485_transport_t *xp,
                                    uint8_t slave_id,
                                    uint8_t func_code,
                                    uint16_t start_reg,
                                    uint16_t reg_count,
                                    uint32_t resp_timeout_ms,
                                    uint8_t *rx_buf,
                                    size_t rx_buf_cap,
                                    size_t *out_rx_len) {
    if (!ctx || !xp || !xp->tx_bytes || !xp->rx_bytes || !rx_buf || rx_buf_cap < 5U) {
        return EB_RS485_ERR_INVALID_ARG;
    }

    uint8_t req[8];
    eb_rs485_build_read_req(slave_id, func_code, start_reg, reg_count, req);

    /* Ensure bus-idle >= t3.5 before transmitting and purge stale RX noise/echo. */
    if (xp->delay_us) {
        xp->delay_us(xp->user_ctx, ctx->effective_t35_us);
    }
    if (xp->flush_rx) {
        xp->flush_rx(xp->user_ctx);
    }

    if (!ctx->auto_direction && xp->set_tx_enable) {
        xp->set_tx_enable(xp->user_ctx, true);
    }

    int tx_rc = xp->tx_bytes(xp->user_ctx, req, sizeof(req));
    uint64_t ts_ms = xp->now_ms ? xp->now_ms(xp->user_ctx) : 0ULL;
    ctx->tx_frames++;
    log_frame(ctx, ts_ms, true, slave_id, func_code, req, sizeof(req), EB_RS485_OK);

    /* Wait for frame serialization + isolator turnaround margin before RX/DE release. */
    if (xp->delay_us) {
        uint32_t tx_us = eb_rs485_frame_tx_time_us(ctx->baud_rate, sizeof(req));
        xp->delay_us(xp->user_ctx, tx_us + ctx->guard_extra_us + ctx->adaptive_extra_us);
    }
    if (!ctx->auto_direction && xp->set_tx_enable) {
        xp->set_tx_enable(xp->user_ctx, false);
    }
    if (tx_rc < 0) {
        return EB_RS485_ERR_TIMEOUT;
    }

    size_t want = eb_rs485_expected_reply_len(func_code, reg_count);
    if (want > rx_buf_cap) {
        want = rx_buf_cap;
    }
    int rx_n = xp->rx_bytes(xp->user_ctx, rx_buf, want,
                            resp_timeout_ms > 0U ? resp_timeout_ms : 150U,
                            ctx->effective_t35_us);
    size_t rx_len = (rx_n > 0) ? (size_t)rx_n : 0U;
    if (out_rx_len) {
        *out_rx_len = rx_len;
    }

    eb_rs485_status_t st = eb_rs485_validate_reply(ctx, slave_id, func_code, rx_buf, rx_len);
    ts_ms = xp->now_ms ? xp->now_ms(xp->user_ctx) : ts_ms;
    log_frame(ctx, ts_ms, false, slave_id, func_code, rx_buf, rx_len, st);
    return st;
}
