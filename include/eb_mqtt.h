/*
 * Copyright (c) 2026 Nitin Chauhan / EdgeBridge-S3
 * SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-EdgeBridge-Commercial
 */

#ifndef EB_MQTT_H
#define EB_MQTT_H

#include "eb_profile.h"
#include "eb_storage.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char device_id[32];
    bool wifi_connected;
    bool mqtt_connected;
    uint32_t published_count;
    uint32_t publish_failures;
    uint32_t reconnect_attempts;
    uint32_t outage_count;
    uint64_t last_publish_ms;
    uint64_t last_reconnect_attempt_ms;
} eb_mqtt_client_t;

typedef int (*eb_mqtt_publish_fn_t)(void *user_ctx, const char *topic,
                                    const char *payload, size_t payload_len);

void eb_mqtt_init(eb_mqtt_client_t *client, const char *device_id);
void eb_mqtt_set_link_state(eb_mqtt_client_t *client, bool wifi_up, bool mqtt_up);
uint32_t eb_mqtt_next_backoff_ms(const eb_mqtt_client_t *client);
bool eb_mqtt_should_reconnect(eb_mqtt_client_t *client, uint64_t now_ms);

/* Serialize one telemetry record (plus matching decoded metrics) into JSON. */
int eb_mqtt_format_record_json(char *out_buf, size_t buf_cap,
                               const char *device_id,
                               const eb_telemetry_record_t *rec,
                               const eb_profile_mgr_t *profiles);

/*
 * FIFO store-and-forward drain: peeks oldest valid records from LittleFS ring
 * and only calls eb_storage_ack_oldest() after publish_fn returns 0 (PUBACK).
 */
size_t eb_mqtt_drain_storage(eb_mqtt_client_t *client,
                             eb_storage_ring_t *storage,
                             const eb_profile_mgr_t *profiles,
                             eb_mqtt_publish_fn_t publish_fn,
                             void *user_ctx,
                             size_t max_batch,
                             char *scratch_buf,
                             size_t scratch_cap);

#ifdef __cplusplus
}
#endif

#endif /* EB_MQTT_H */
