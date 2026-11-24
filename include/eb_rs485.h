#ifndef EB_RS485_H
#define EB_RS485_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EB_RS485_ISOLATOR_BAUD_CEILING 115200U
#define EB_RS485_MIN_T35_US            1750U
#define EB_RS485_MAX_ADAPTIVE_EXTRA_US 5000U
#define EB_RS485_FRAME_LOG_CAP         16U

typedef enum {
    EB_RS485_OK = 0,
    EB_RS485_ERR_TRUNCATED = -1,
    EB_RS485_ERR_CRC = -2,
    EB_RS485_ERR_SLAVE_MISMATCH = -3,
    EB_RS485_ERR_TIMEOUT = -4,
    EB_RS485_ERR_INVALID_ARG = -5,
    EB_RS485_ERR_EXCEPTION = -6
} eb_rs485_status_t;

typedef struct {
    uint64_t timestamp_ms;
    bool is_tx;
    uint8_t slave_id;
    uint8_t func_code;
    uint8_t len;
    int8_t status;
    uint8_t preview[12];
} eb_rs485_frame_log_t;

typedef struct {
    uint32_t baud_rate;
    uint32_t guard_extra_us;
    uint32_t adaptive_extra_us;
    uint32_t effective_t35_us;
    bool auto_direction;      /* True: Waveshare HW auto-dir; False: toggle DE GPIO */
    bool baud_ceiling_warn;   /* True if requested baud > 115,200 bps isolator limit */
    uint32_t tx_frames;
    uint32_t rx_frames;
    uint32_t crc_errors;
    uint32_t framing_clipped_errors;
    uint32_t timeout_errors;
    uint32_t exception_responses;
    uint8_t last_exception_code;
    uint8_t consecutive_bus_errors;
    eb_rs485_frame_log_t recent_frames[EB_RS485_FRAME_LOG_CAP];
    size_t recent_head;
    size_t recent_count;
} eb_rs485_ctx_t;

/* Transport abstraction so deterministic t3.5 & reply validation share one code path. */
typedef struct {
    int (*set_baud)(void *user_ctx, uint32_t baud_rate);
    void (*flush_rx)(void *user_ctx);
    void (*set_tx_enable)(void *user_ctx, bool enable);
    int (*tx_bytes)(void *user_ctx, const uint8_t *buf, size_t len);
    int (*rx_bytes)(void *user_ctx, uint8_t *buf, size_t max_len,
                    uint32_t resp_timeout_ms, uint32_t inter_char_timeout_us);
    void (*delay_us)(void *user_ctx, uint32_t us);
    uint64_t (*now_ms)(void *user_ctx);
    void *user_ctx;
} eb_rs485_transport_t;

void eb_rs485_init(eb_rs485_ctx_t *ctx, uint32_t baud_rate, uint32_t guard_extra_us,
                   bool auto_direction, bool strict_cap);

/* Computes Modbus RTU t3.5 inter-frame guard time in microseconds + isolator margin. */
uint32_t eb_rs485_calc_t35_us(uint32_t baud_rate, uint32_t extra_margin_us);

/* Computes serialization duration of len bytes at baud_rate (11 bits/byte). */
uint32_t eb_rs485_frame_tx_time_us(uint32_t baud_rate, size_t len);

/* Standard Modbus RTU CRC-16 (poly 0xA001, init 0xFFFF). */
uint16_t eb_rs485_crc16(const uint8_t *data, size_t len);

/* Builds a Modbus RTU Read Coils/Inputs/Holding/Input Registers request (8 bytes). */
size_t eb_rs485_build_read_req(uint8_t slave_id, uint8_t func_code,
                               uint16_t start_reg, uint16_t reg_count,
                               uint8_t out_frame[8]);

/* Builds a Modbus RTU Write Single Register (0x06) request (8 bytes). */
size_t eb_rs485_build_write_single_req(uint8_t slave_id, uint16_t reg_addr,
                                       uint16_t value, uint8_t out_frame[8]);

/* Expected normal reply byte length (excluding exception frames, which are 5 bytes). */
size_t eb_rs485_expected_reply_len(uint8_t func_code, uint16_t reg_or_coil_count);

/* Validates a Modbus RTU reply, updating diagnostic counters and adaptive margin. */
eb_rs485_status_t eb_rs485_validate_reply(eb_rs485_ctx_t *ctx,
                                          uint8_t expected_slave,
                                          uint8_t expected_func,
                                          const uint8_t *rx_buf,
                                          size_t rx_len);

/* Executes a full half-duplex Modbus RTU transaction with t3.5 turnaround guard. */
eb_rs485_status_t eb_rs485_transact(eb_rs485_ctx_t *ctx,
                                    const eb_rs485_transport_t *xp,
                                    uint8_t slave_id,
                                    uint8_t func_code,
                                    uint16_t start_reg,
                                    uint16_t reg_count,
                                    uint32_t resp_timeout_ms,
                                    uint8_t *rx_buf,
                                    size_t rx_buf_cap,
                                    size_t *out_rx_len);

#ifdef __cplusplus
}
#endif

#endif /* EB_RS485_H */
