#ifndef EB_BLE_PROV_H
#define EB_BLE_PROV_H

#include "eb_config.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    EB_BLE_STATE_DISABLED = 0,
    EB_BLE_STATE_ADVERTISING = 1,
    EB_BLE_STATE_CONNECTED = 2,
    EB_BLE_STATE_PROVISIONED_SHUTDOWN = 3
} eb_ble_state_t;

typedef void (*eb_ble_radio_off_cb_t)(void *user_ctx);

typedef struct {
    eb_ble_state_t state;
    uint32_t start_sec;
    uint32_t timeout_sec;
    bool radio_active;
    bool ap_fallback_active;
    char ap_ssid[33];
    eb_ble_radio_off_cb_t on_radio_off;
    void *user_ctx;
} eb_ble_prov_t;

void eb_ble_prov_init(eb_ble_prov_t *prov, bool already_provisioned,
                      uint32_t now_sec, uint32_t timeout_sec,
                      eb_ble_radio_off_cb_t radio_off_cb, void *user_ctx);

/* Apply Wi-Fi & MQTT credentials via BLE/AP and immediately shut down BLE radio. */
int eb_ble_prov_apply_credentials(eb_ble_prov_t *prov, eb_config_mgr_t *cfg_mgr,
                                  const char *ssid, const char *pass,
                                  const char *mqtt_broker, uint16_t mqtt_port,
                                  uint32_t now_sec);

/* Periodic tick; shuts down BLE on timeout to reclaim internal SRAM. */
void eb_ble_prov_tick(eb_ble_prov_t *prov, uint32_t now_sec);

/* Explicitly re-open ephemeral provisioning window (e.g., button press or AP mode). */
void eb_ble_prov_enable_ap_fallback(eb_ble_prov_t *prov, const char *ap_ssid, bool active);

/* Shut down BLE radio immediately after out-of-band (e.g., SoftAP Web UI) provisioning. */
void eb_ble_prov_notify_provisioned(eb_ble_prov_t *prov);

#ifdef __cplusplus
}
#endif

#endif /* EB_BLE_PROV_H */
