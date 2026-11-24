#ifndef EB_SCANNER_H
#define EB_SCANNER_H

#include "eb_rs485.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EB_SCAN_MAX_RS485_HITS 16U
#define EB_SCAN_MAX_CAN_IDS    16U

typedef struct {
    uint32_t baud_rate;
    uint8_t slave_id;
    uint8_t probe_func;
    uint16_t probe_reg;
    bool is_exception;
    uint8_t exception_code;
} eb_scan_rs485_hit_t;

typedef struct {
    bool scan_completed;
    uint32_t bauds_probed;
    uint32_t probes_sent;
    size_t hit_count;
    eb_scan_rs485_hit_t hits[EB_SCAN_MAX_RS485_HITS];
    uint32_t detected_can_bitrate;
    size_t can_id_count;
    uint32_t can_ids[EB_SCAN_MAX_CAN_IDS];
} eb_scan_result_t;

typedef struct {
    int (*set_listen_bitrate)(void *user_ctx, uint32_t bitrate);
    int (*listen_frames)(void *user_ctx, uint32_t window_ms,
                         uint32_t *out_ids, size_t max_ids, size_t *out_count);
    void *user_ctx;
} eb_can_scan_transport_t;

/*
 * Auto-detect RS-485 baud rates (9600..115200) and Modbus slave IDs.
 * Both EB_RS485_OK and valid-CRC EB_RS485_ERR_EXCEPTION count as live node hits.
 */
int eb_scanner_run_rs485(eb_rs485_ctx_t *rs485,
                         const eb_rs485_transport_t *xp,
                         uint8_t min_slave_id,
                         uint8_t max_slave_id,
                         eb_scan_result_t *out_result);

/*
 * Listen-only CAN bitrate auto-detection across [500000, 250000, 125000, 1000000]
 * so a live solar BMS/inverter bus is never disturbed during commissioning.
 */
int eb_scanner_run_can(const eb_can_scan_transport_t *xp,
                       uint32_t window_per_rate_ms,
                       eb_scan_result_t *out_result);

#ifdef __cplusplus
}
#endif

#endif /* EB_SCANNER_H */
