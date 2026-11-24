/*
 * Copyright (c) 2026 Nitin Chauhan / EdgeBridge-S3
 * SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-EdgeBridge-Commercial
 */

#ifndef EB_OTA_H
#define EB_OTA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    EB_OTA_IDLE = 0,
    EB_OTA_DOWNLOADING = 1,
    EB_OTA_STAGED_PENDING_REBOOT = 2,
    EB_OTA_FAILED = 3
} eb_ota_download_status_t;

typedef struct {
    int (*begin)(void *ctx, size_t expected_size);
    int (*write_chunk)(void *ctx, size_t offset, const uint8_t *data, size_t len);
    int (*finish_and_request_upgrade)(void *ctx);
    void *ctx;
} eb_ota_flash_ops_t;

typedef struct {
    bool image_confirmed;
    bool network_healthy;
    bool bus_healthy;
    uint32_t boot_count;
    uint32_t required_stable_sec;
    uint32_t healthy_since_sec;
    bool health_timer_armed;

    eb_ota_download_status_t dl_status;
    char pending_url[128];
    size_t expected_size;
    size_t bytes_received;
    uint32_t expected_crc32;
    uint32_t running_crc32;
    const eb_ota_flash_ops_t *flash_ops;
} eb_ota_state_t;

void eb_ota_init(eb_ota_state_t *ota, bool already_confirmed);
void eb_ota_init_with_gate(eb_ota_state_t *ota, bool already_confirmed,
                           uint32_t required_stable_sec,
                           const eb_ota_flash_ops_t *flash_ops);

void eb_ota_report_health(eb_ota_state_t *ota, bool net_ok, bool bus_ok);
void eb_ota_report_health_at(eb_ota_state_t *ota, bool net_ok, bool bus_ok, uint32_t now_sec);

/* Confirms MCUboot slot0 only when network + bus health checks pass (and stability window elapsed). */
bool eb_ota_try_confirm(eb_ota_state_t *ota);
bool eb_ota_try_confirm_at(eb_ota_state_t *ota, uint32_t now_sec);

/* Resumable byte-range OTA staging state machine. */
int eb_ota_begin_download(eb_ota_state_t *ota, const char *url,
                          size_t expected_size, uint32_t expected_crc32);
int eb_ota_ingest_chunk(eb_ota_state_t *ota, size_t offset,
                        const uint8_t *data, size_t len);
int eb_ota_finalize(eb_ota_state_t *ota);
const char *eb_ota_status_str(eb_ota_download_status_t status);

#ifdef __cplusplus
}
#endif

#endif /* EB_OTA_H */
