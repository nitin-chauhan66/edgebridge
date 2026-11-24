#ifndef EB_CONFIG_H
#define EB_CONFIG_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EB_CONFIG_WATCHDOG_DEFAULT_SEC 60U
#define EB_CONFIG_MAGIC                0x45424346U /* "EBCF" */
#define EB_CONFIG_PERSIST_MAGIC        0x45425053U /* "EBPS" */

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t rs485_baud;
    uint32_t rs485_guard_extra_us;
    bool rs485_auto_direction;
    bool rs485_strict_baud_cap;
    uint32_t can_bitrate;
    uint16_t poll_interval_ms;
    char wifi_ssid[33];
    char wifi_pass[65];
    char mqtt_broker[65];
    uint16_t mqtt_port;
    char active_profile[32];
    bool provisioned;
    uint32_t crc32;
} eb_config_t;

typedef struct {
    uint32_t magic;
    eb_config_t active;
    eb_config_t backup;
    bool pending_confirm;
    uint32_t rollback_count;
    uint32_t crc32;
} eb_config_persist_blob_t;

typedef struct {
    int (*load)(void *ctx, eb_config_persist_blob_t *out_blob);
    int (*save)(void *ctx, const eb_config_persist_blob_t *blob);
    void *ctx;
} eb_config_backend_t;

typedef struct {
    eb_config_t active;
    eb_config_t backup;
    bool pending_confirm;
    uint32_t staged_at_sec;
    uint32_t watchdog_sec;
    uint32_t rollback_count;
    const eb_config_backend_t *backend;
} eb_config_mgr_t;

void eb_config_init(eb_config_mgr_t *mgr, uint32_t watchdog_sec);

/* Load dual-bank config from NVS/file; auto-reverts if pending_confirm survived a crash/reboot. */
int eb_config_attach_backend(eb_config_mgr_t *mgr, const eb_config_backend_t *backend);

uint32_t eb_config_crc32(const eb_config_t *cfg);
bool eb_config_validate(const eb_config_t *cfg);

/* Stage a new config; starts the 60s rollback watchdog and persists pending state. */
int eb_config_stage(eb_config_mgr_t *mgr, const eb_config_t *new_cfg, uint32_t now_sec);

/* Confirm the staged config (e.g., after MQTT/Web UI reachability is verified). */
int eb_config_confirm(eb_config_mgr_t *mgr);

/* Periodic tick; returns true if an automatic rollback occurred. */
bool eb_config_tick(eb_config_mgr_t *mgr, uint32_t now_sec);

/* Remaining seconds before watchdog auto-reverts, or 0 if confirmed. */
uint32_t eb_config_remaining_watchdog_sec(const eb_config_mgr_t *mgr, uint32_t now_sec);

#ifdef __cplusplus
}
#endif

#endif /* EB_CONFIG_H */
