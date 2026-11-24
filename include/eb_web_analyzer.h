/*
 * Copyright (c) 2026 Nitin Chauhan / EdgeBridge-S3
 * SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-EdgeBridge-Commercial
 */

#ifndef EB_WEB_ANALYZER_H
#define EB_WEB_ANALYZER_H

#include "eb_ble_prov.h"
#include "eb_can.h"
#include "eb_config.h"
#include "eb_mem.h"
#include "eb_mqtt.h"
#include "eb_ota.h"
#include "eb_profile.h"
#include "eb_rs485.h"
#include "eb_scanner.h"
#include "eb_storage.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    eb_rs485_ctx_t *rs485;
    eb_can_diag_t *can;
    eb_storage_ring_t *storage;
    eb_config_mgr_t *cfg;
    eb_ble_prov_t *ble;
    eb_ota_state_t *ota;
    eb_mqtt_client_t *mqtt;
    eb_profile_mgr_t *profiles;
    eb_scan_result_t *scan;
    const eb_mem_stats_t *mem;
    uint32_t (*now_sec_cb)(void *user_ctx);
    int (*trigger_scan_cb)(void *user_ctx);
    void *user_ctx;
} eb_web_ctx_t;

/* Formats live bus diagnostics JSON into out_buf for /api/v1/diagnostics. */
int eb_web_format_diagnostics_json(char *out_buf, size_t buf_len,
                                   const eb_rs485_ctx_t *rs485,
                                   const eb_can_diag_t *can,
                                   const eb_storage_ring_t *storage,
                                   const eb_config_mgr_t *cfg,
                                   const eb_ble_prov_t *ble);

/* Extended diagnostics JSON including MQTT, OTA, Memory/PSRAM, and Watchdog countdown. */
int eb_web_format_full_diagnostics_json(char *out_buf, size_t buf_len,
                                        const eb_web_ctx_t *ctx);

/* Formats live decoded BMS/Inverter values + recent RS-485 & CAN sniffer frames. */
int eb_web_format_telemetry_json(char *out_buf, size_t buf_len,
                                 const eb_web_ctx_t *ctx);

/*
 * Portable HTTP/1.1 REST router used by both Zephyr HTTP server and host integration tests.
 * Routes:
 *   GET  /                    -> Mobile-first Bus Analyzer & 5-Min Commissioning UI
 *   GET  /api/v1/diagnostics  -> Bus, storage, config watchdog, SRAM/PSRAM, MQTT, OTA
 *   GET  /api/v1/telemetry    -> Live decoded metrics + raw frame sniffer
 *   GET  /api/v1/scan         -> Last RS-485 & CAN auto-scan results
 *   POST /api/v1/scan         -> Trigger RS-485 baud/ID & CAN bitrate auto-scan
 *   GET  /api/v1/profiles     -> Built-in & active device profiles
 *   POST /api/v1/profiles     -> Load built-in ({"builtin":"..."}) or raw profile JSON
 *   GET  /api/v1/config       -> Current active config & watchdog state
 *   POST /api/v1/config       -> Stage new config (starts 60s watchdog)
 *   POST /api/v1/config/confirm -> Confirm staged config (disarms 60s rollback)
 *   GET  /api/v1/ota          -> OTA confirmation & staging status
 */
int eb_web_handle_http(eb_web_ctx_t *ctx,
                       const char *method,
                       const char *path,
                       const char *body,
                       size_t body_len,
                       int *out_status,
                       const char **out_content_type,
                       char *out_buf,
                       size_t out_cap);

/* Returns pointer to embedded mobile-first HTML Bus Analyzer UI. */
const char *eb_web_get_analyzer_html(void);

#ifdef __cplusplus
}
#endif

#endif /* EB_WEB_ANALYZER_H */
