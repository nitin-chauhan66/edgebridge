/*
 * Copyright (c) 2026 Nitin Chauhan / EdgeBridge-S3
 * SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-EdgeBridge-Commercial
 */

#include "eb_ble_prov.h"
#include <stdio.h>
#include <string.h>

static void shutdown_ble_radio(eb_ble_prov_t *prov) {
    if (prov->radio_active) {
        prov->radio_active = false;
        if (prov->on_radio_off) {
            prov->on_radio_off(prov->user_ctx);
        }
    }
}

void eb_ble_prov_init(eb_ble_prov_t *prov, bool already_provisioned,
                      uint32_t now_sec, uint32_t timeout_sec,
                      eb_ble_radio_off_cb_t radio_off_cb, void *user_ctx) {
    if (!prov) {
        return;
    }
    memset(prov, 0, sizeof(*prov));
    prov->start_sec = now_sec;
    prov->timeout_sec = (timeout_sec > 0U) ? timeout_sec : 300U;
    prov->on_radio_off = radio_off_cb;
    prov->user_ctx = user_ctx;
    snprintf(prov->ap_ssid, sizeof(prov->ap_ssid), "EdgeBridge-S3-AP");

    if (already_provisioned) {
        prov->state = EB_BLE_STATE_DISABLED;
        prov->radio_active = false;
        prov->ap_fallback_active = false;
    } else {
        prov->state = EB_BLE_STATE_ADVERTISING;
        prov->radio_active = true;
        prov->ap_fallback_active = true;
    }
}

int eb_ble_prov_apply_credentials(eb_ble_prov_t *prov, eb_config_mgr_t *cfg_mgr,
                                  const char *ssid, const char *pass,
                                  const char *mqtt_broker, uint16_t mqtt_port,
                                  uint32_t now_sec) {
    if (!prov || !cfg_mgr || !ssid || ssid[0] == '\0' || !mqtt_broker || mqtt_broker[0] == '\0') {
        return -1;
    }

    eb_config_t next = cfg_mgr->active;
    snprintf(next.wifi_ssid, sizeof(next.wifi_ssid), "%s", ssid);
    if (pass) {
        snprintf(next.wifi_pass, sizeof(next.wifi_pass), "%s", pass);
    } else {
        next.wifi_pass[0] = '\0';
    }
    snprintf(next.mqtt_broker, sizeof(next.mqtt_broker), "%s", mqtt_broker);
    next.mqtt_port = (mqtt_port > 0U) ? mqtt_port : 1883U;
    next.provisioned = true;

    int rc = eb_config_stage(cfg_mgr, &next, now_sec);
    if (rc != 0) {
        return rc;
    }

    /* Immediately shut down BLE stack to reclaim internal SRAM for Wi-Fi/TLS */
    prov->state = EB_BLE_STATE_PROVISIONED_SHUTDOWN;
    prov->ap_fallback_active = false;
    shutdown_ble_radio(prov);
    return 0;
}

void eb_ble_prov_tick(eb_ble_prov_t *prov, uint32_t now_sec) {
    if (!prov || !prov->radio_active) {
        return;
    }
    if ((now_sec - prov->start_sec) >= prov->timeout_sec) {
        prov->state = EB_BLE_STATE_DISABLED;
        shutdown_ble_radio(prov);
    }
}

void eb_ble_prov_enable_ap_fallback(eb_ble_prov_t *prov, const char *ap_ssid, bool active) {
    if (!prov) {
        return;
    }
    if (ap_ssid && ap_ssid[0] != '\0') {
        snprintf(prov->ap_ssid, sizeof(prov->ap_ssid), "%s", ap_ssid);
    }
    prov->ap_fallback_active = active;
}

void eb_ble_prov_notify_provisioned(eb_ble_prov_t *prov) {
    if (!prov) {
        return;
    }
    prov->state = EB_BLE_STATE_PROVISIONED_SHUTDOWN;
    prov->ap_fallback_active = false;
    shutdown_ble_radio(prov);
}
