/*
 * Copyright (c) 2026 Nitin Chauhan / EdgeBridge-S3
 * SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-EdgeBridge-Commercial
 */

#include "eb_ota.h"
#include <stdio.h>
#include <string.h>

static uint32_t crc32_update(uint32_t crc, const uint8_t *data, size_t len) {
    uint32_t c = ~crc;
    for (size_t i = 0U; i < len; i++) {
        c ^= data[i];
        for (int b = 0; b < 8; b++) {
            uint32_t mask = (uint32_t)(-(int32_t)(c & 1U));
            c = (c >> 1) ^ (0xEDB88320U & mask);
        }
    }
    return ~c;
}

void eb_ota_init(eb_ota_state_t *ota, bool already_confirmed) {
    eb_ota_init_with_gate(ota, already_confirmed, 0U, NULL);
}

void eb_ota_init_with_gate(eb_ota_state_t *ota, bool already_confirmed,
                           uint32_t required_stable_sec,
                           const eb_ota_flash_ops_t *flash_ops) {
    if (!ota) {
        return;
    }
    memset(ota, 0, sizeof(*ota));
    ota->image_confirmed = already_confirmed;
    ota->boot_count = 1U;
    ota->required_stable_sec = required_stable_sec;
    ota->flash_ops = flash_ops;
    ota->dl_status = EB_OTA_IDLE;
}

void eb_ota_report_health(eb_ota_state_t *ota, bool net_ok, bool bus_ok) {
    eb_ota_report_health_at(ota, net_ok, bus_ok, 0U);
}

void eb_ota_report_health_at(eb_ota_state_t *ota, bool net_ok, bool bus_ok, uint32_t now_sec) {
    if (!ota) {
        return;
    }
    ota->network_healthy = net_ok;
    ota->bus_healthy = bus_ok;
    if (net_ok && bus_ok) {
        if (!ota->health_timer_armed) {
            ota->health_timer_armed = true;
            ota->healthy_since_sec = now_sec;
        }
    } else {
        ota->health_timer_armed = false;
        ota->healthy_since_sec = 0U;
    }
}

bool eb_ota_try_confirm(eb_ota_state_t *ota) {
    return eb_ota_try_confirm_at(ota, 0U);
}

bool eb_ota_try_confirm_at(eb_ota_state_t *ota, uint32_t now_sec) {
    if (!ota) {
        return false;
    }
    if (ota->image_confirmed) {
        return true;
    }
    if (!ota->network_healthy || !ota->bus_healthy) {
        return false;
    }
    if (ota->required_stable_sec > 0U) {
        if (!ota->health_timer_armed ||
            (now_sec - ota->healthy_since_sec) < ota->required_stable_sec) {
            return false;
        }
    }
    ota->image_confirmed = true;
    return true;
}

int eb_ota_begin_download(eb_ota_state_t *ota, const char *url,
                          size_t expected_size, uint32_t expected_crc32) {
    if (!ota || !url || url[0] == '\0' || expected_size == 0U) {
        return -1;
    }
    snprintf(ota->pending_url, sizeof(ota->pending_url), "%s", url);
    ota->expected_size = expected_size;
    ota->expected_crc32 = expected_crc32;
    ota->bytes_received = 0U;
    ota->running_crc32 = 0U;
    ota->dl_status = EB_OTA_DOWNLOADING;

    if (ota->flash_ops && ota->flash_ops->begin) {
        if (ota->flash_ops->begin(ota->flash_ops->ctx, expected_size) != 0) {
            ota->dl_status = EB_OTA_FAILED;
            return -2;
        }
    }
    return 0;
}

int eb_ota_ingest_chunk(eb_ota_state_t *ota, size_t offset,
                        const uint8_t *data, size_t len) {
    if (!ota || !data || len == 0U || ota->dl_status != EB_OTA_DOWNLOADING) {
        return -1;
    }
    /* Support resumable byte-range alignment: ignore duplicate prefix, reject gaps */
    if (offset > ota->bytes_received) {
        return -2;
    }
    if (offset + len <= ota->bytes_received) {
        return 0;
    }
    size_t skip = ota->bytes_received - offset;
    const uint8_t *payload = data + skip;
    size_t write_len = len - skip;

    if (ota->bytes_received + write_len > ota->expected_size) {
        ota->dl_status = EB_OTA_FAILED;
        return -3;
    }
    if (ota->flash_ops && ota->flash_ops->write_chunk) {
        if (ota->flash_ops->write_chunk(ota->flash_ops->ctx, ota->bytes_received,
                                        payload, write_len) != 0) {
            ota->dl_status = EB_OTA_FAILED;
            return -4;
        }
    }
    ota->running_crc32 = crc32_update(ota->running_crc32, payload, write_len);
    ota->bytes_received += write_len;
    return 0;
}

int eb_ota_finalize(eb_ota_state_t *ota) {
    if (!ota || ota->dl_status != EB_OTA_DOWNLOADING) {
        return -1;
    }
    if (ota->bytes_received != ota->expected_size) {
        ota->dl_status = EB_OTA_FAILED;
        return -2;
    }
    if (ota->expected_crc32 != 0U && ota->running_crc32 != ota->expected_crc32) {
        ota->dl_status = EB_OTA_FAILED;
        return -3;
    }
    if (ota->flash_ops && ota->flash_ops->finish_and_request_upgrade) {
        if (ota->flash_ops->finish_and_request_upgrade(ota->flash_ops->ctx) != 0) {
            ota->dl_status = EB_OTA_FAILED;
            return -4;
        }
    }
    ota->dl_status = EB_OTA_STAGED_PENDING_REBOOT;
    return 0;
}

const char *eb_ota_status_str(eb_ota_download_status_t status) {
    switch (status) {
        case EB_OTA_IDLE:                  return "IDLE";
        case EB_OTA_DOWNLOADING:           return "DOWNLOADING";
        case EB_OTA_STAGED_PENDING_REBOOT: return "STAGED_PENDING_REBOOT";
        case EB_OTA_FAILED:                return "FAILED";
        default:                           return "UNKNOWN";
    }
}
