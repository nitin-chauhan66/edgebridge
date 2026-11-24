/*
 * Copyright (c) 2026 Nitin Chauhan / EdgeBridge-S3
 * SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-EdgeBridge-Commercial
 */

#include "eb_config.h"
#include "eb_crc.h"
#include <stddef.h>
#include <stdio.h>
#include <string.h>

static uint32_t persist_blob_crc32(const eb_config_persist_blob_t *blob) {
    return eb_crc32_ieee(blob, offsetof(eb_config_persist_blob_t, crc32));
}

static bool valid_can_bitrate(uint32_t bitrate) {
    return bitrate == 125000U || bitrate == 250000U ||
           bitrate == 500000U || bitrate == 1000000U;
}

static void save_if_backed(const eb_config_mgr_t *mgr) {
    if (!mgr || !mgr->backend || !mgr->backend->save) {
        return;
    }
    eb_config_persist_blob_t blob;
    memset(&blob, 0, sizeof(blob));
    blob.magic = EB_CONFIG_PERSIST_MAGIC;
    blob.active = mgr->active;
    blob.backup = mgr->backup;
    blob.pending_confirm = mgr->pending_confirm;
    blob.rollback_count = mgr->rollback_count;
    blob.crc32 = persist_blob_crc32(&blob);
    (void)mgr->backend->save(mgr->backend->ctx, &blob);
}

uint32_t eb_config_crc32(const eb_config_t *cfg) {
    if (!cfg) {
        return 0U;
    }
    return eb_crc32_ieee(cfg, offsetof(eb_config_t, crc32));
}

bool eb_config_validate(const eb_config_t *cfg) {
    if (!cfg) {
        return false;
    }
    if (cfg->magic != EB_CONFIG_MAGIC) {
        return false;
    }
    if (cfg->rs485_baud < 1200U || cfg->rs485_baud > 921600U) {
        return false;
    }
    if (!valid_can_bitrate(cfg->can_bitrate)) {
        return false;
    }
    if (cfg->poll_interval_ms < 100U || cfg->poll_interval_ms > 60000U) {
        return false;
    }
    if (cfg->mqtt_port == 0U) {
        return false;
    }
    return cfg->crc32 == eb_config_crc32(cfg);
}

void eb_config_init(eb_config_mgr_t *mgr, uint32_t watchdog_sec) {
    if (!mgr) {
        return;
    }
    memset(mgr, 0, sizeof(*mgr));
    mgr->watchdog_sec = (watchdog_sec > 0U) ? watchdog_sec : EB_CONFIG_WATCHDOG_DEFAULT_SEC;

    mgr->active.magic = EB_CONFIG_MAGIC;
    mgr->active.version = 1U;
    mgr->active.rs485_baud = 9600U;
    mgr->active.rs485_guard_extra_us = 250U;
    mgr->active.rs485_auto_direction = true; /* Waveshare ESP32-S3 uses HW auto-direction */
    mgr->active.rs485_strict_baud_cap = true;
    mgr->active.can_bitrate = 500000U;
    mgr->active.poll_interval_ms = 1000U;
    mgr->active.mqtt_port = 1883U;
    snprintf(mgr->active.active_profile, sizeof(mgr->active.active_profile), "jk_bms_modbus_v1");
    mgr->active.provisioned = false;
    mgr->active.crc32 = eb_config_crc32(&mgr->active);

    mgr->backup = mgr->active;
}

int eb_config_attach_backend(eb_config_mgr_t *mgr, const eb_config_backend_t *backend) {
    if (!mgr || !backend || !backend->load || !backend->save) {
        return -1;
    }
    mgr->backend = backend;

    eb_config_persist_blob_t blob;
    if (backend->load(backend->ctx, &blob) == 0 &&
        blob.magic == EB_CONFIG_PERSIST_MAGIC &&
        blob.crc32 == persist_blob_crc32(&blob) &&
        eb_config_validate(&blob.backup)) {
        mgr->backup = blob.backup;
        mgr->rollback_count = blob.rollback_count;

        /*
         * If the previous boot crashed/rebooted while a staged config was still
         * unconfirmed, immediately revert to backup to prevent remote lockout.
         */
        if (blob.pending_confirm || !eb_config_validate(&blob.active)) {
            mgr->active = mgr->backup;
            mgr->pending_confirm = false;
            mgr->rollback_count++;
            save_if_backed(mgr);
        } else {
            mgr->active = blob.active;
            mgr->pending_confirm = false;
        }
        return 0;
    }

    save_if_backed(mgr);
    return 0;
}

int eb_config_stage(eb_config_mgr_t *mgr, const eb_config_t *new_cfg, uint32_t now_sec) {
    if (!mgr || !new_cfg) {
        return -1;
    }
    eb_config_t candidate = *new_cfg;
    candidate.magic = EB_CONFIG_MAGIC;
    candidate.version = mgr->active.version + 1U;
    candidate.crc32 = eb_config_crc32(&candidate);

    if (!eb_config_validate(&candidate)) {
        return -2;
    }

    mgr->backup = mgr->active;
    mgr->active = candidate;
    mgr->pending_confirm = true;
    mgr->staged_at_sec = now_sec;
    save_if_backed(mgr);
    return 0;
}

int eb_config_confirm(eb_config_mgr_t *mgr) {
    if (!mgr || !mgr->pending_confirm) {
        return -1;
    }
    mgr->backup = mgr->active;
    mgr->pending_confirm = false;
    save_if_backed(mgr);
    return 0;
}

bool eb_config_tick(eb_config_mgr_t *mgr, uint32_t now_sec) {
    if (!mgr || !mgr->pending_confirm) {
        return false;
    }
    if ((now_sec - mgr->staged_at_sec) >= mgr->watchdog_sec) {
        /* 60s watchdog expired without confirmation: auto-revert to backup */
        mgr->active = mgr->backup;
        mgr->pending_confirm = false;
        mgr->rollback_count++;
        save_if_backed(mgr);
        return true;
    }
    return false;
}

uint32_t eb_config_remaining_watchdog_sec(const eb_config_mgr_t *mgr, uint32_t now_sec) {
    if (!mgr || !mgr->pending_confirm) {
        return 0U;
    }
    uint32_t elapsed = now_sec - mgr->staged_at_sec;
    if (elapsed >= mgr->watchdog_sec) {
        return 0U;
    }
    return mgr->watchdog_sec - elapsed;
}
