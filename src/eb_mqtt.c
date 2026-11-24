#include "eb_mqtt.h"
#include <stdio.h>
#include <string.h>

void eb_mqtt_init(eb_mqtt_client_t *client, const char *device_id) {
    if (!client) {
        return;
    }
    memset(client, 0, sizeof(*client));
    snprintf(client->device_id, sizeof(client->device_id), "%s",
             (device_id && device_id[0] != '\0') ? device_id : "edgebridge-s3");
}

void eb_mqtt_set_link_state(eb_mqtt_client_t *client, bool wifi_up, bool mqtt_up) {
    if (!client) {
        return;
    }
    bool was_up = client->mqtt_connected;
    client->wifi_connected = wifi_up;
    client->mqtt_connected = wifi_up && mqtt_up;
    if (was_up && !client->mqtt_connected) {
        client->outage_count++;
    }
    if (client->mqtt_connected) {
        client->reconnect_attempts = 0U;
    }
}

uint32_t eb_mqtt_next_backoff_ms(const eb_mqtt_client_t *client) {
    if (!client) {
        return 1000U;
    }
    uint32_t shift = client->reconnect_attempts < 5U ? client->reconnect_attempts : 5U;
    uint32_t delay = 1000U << shift;
    return (delay > 30000U) ? 30000U : delay;
}

bool eb_mqtt_should_reconnect(eb_mqtt_client_t *client, uint64_t now_ms) {
    if (!client || !client->wifi_connected || client->mqtt_connected) {
        return false;
    }
    uint32_t backoff = eb_mqtt_next_backoff_ms(client);
    if (client->last_reconnect_attempt_ms == 0ULL ||
        (now_ms - client->last_reconnect_attempt_ms) >= backoff) {
        client->last_reconnect_attempt_ms = now_ms;
        client->reconnect_attempts++;
        return true;
    }
    return false;
}

int eb_mqtt_format_record_json(char *out_buf, size_t buf_cap,
                               const char *device_id,
                               const eb_telemetry_record_t *rec,
                               const eb_profile_mgr_t *profiles) {
    if (!out_buf || buf_cap == 0U || !rec) {
        return -1;
    }
    int pos = snprintf(
        out_buf, buf_cap,
        "{\"device_id\":\"%s\",\"seq\":%u,\"ts_ms\":%llu,"
        "\"bus\":\"%s\",\"addr\":%u,\"can_id\":%u,\"raw_hex\":\"",
        device_id ? device_id : "edgebridge-s3",
        rec->seq,
        (unsigned long long)rec->timestamp_ms,
        (rec->bus == EB_BUS_CAN) ? "can" : "rs485",
        (unsigned)rec->addr,
        rec->can_id);
    if (pos < 0 || (size_t)pos >= buf_cap) {
        return -1;
    }

    size_t hex_len = (rec->len <= EB_TELEMETRY_MAX_PAYLOAD) ? rec->len : EB_TELEMETRY_MAX_PAYLOAD;
    for (size_t i = 0U; i < hex_len; i++) {
        int n = snprintf(out_buf + pos, buf_cap - (size_t)pos, "%02X", rec->payload[i]);
        if (n < 0 || (size_t)(pos + n) >= buf_cap) {
            return -1;
        }
        pos += n;
    }

    int n = snprintf(out_buf + pos, buf_cap - (size_t)pos, "\",\"metrics\":{");
    if (n < 0 || (size_t)(pos + n) >= buf_cap) {
        return -1;
    }
    pos += n;

    bool first_metric = true;
    if (profiles) {
        for (size_t i = 0U; i < profiles->latest_count; i++) {
            const eb_decoded_value_t *m = &profiles->latest[i];
            if (!m->valid || m->bus != rec->bus) {
                continue;
            }
            if (rec->bus == EB_BUS_RS485 && m->addr != rec->addr) {
                continue;
            }
            if (rec->bus == EB_BUS_CAN && rec->can_id != 0U && m->can_id != rec->can_id) {
                continue;
            }
            n = snprintf(out_buf + pos, buf_cap - (size_t)pos,
                         "%s\"%s\":%.3f",
                         first_metric ? "" : ",",
                         m->metric_name,
                         m->value);
            if (n < 0 || (size_t)(pos + n) >= buf_cap) {
                return -1;
            }
            pos += n;
            first_metric = false;
        }
    }

    n = snprintf(out_buf + pos, buf_cap - (size_t)pos, "}}");
    if (n < 0 || (size_t)(pos + n) >= buf_cap) {
        return -1;
    }
    return pos + n;
}

size_t eb_mqtt_drain_storage(eb_mqtt_client_t *client,
                             eb_storage_ring_t *storage,
                             const eb_profile_mgr_t *profiles,
                             eb_mqtt_publish_fn_t publish_fn,
                             void *user_ctx,
                             size_t max_batch,
                             char *scratch_buf,
                             size_t scratch_cap) {
    if (!client || !storage || !publish_fn || !scratch_buf || scratch_cap < 128U) {
        return 0U;
    }
    if (!client->mqtt_connected) {
        return 0U;
    }

    size_t drained = 0U;
    size_t limit = (max_batch > 0U) ? max_batch : 16U;
    char topic[96];
    snprintf(topic, sizeof(topic), "edgebridge/%s/telemetry", client->device_id);

    while (drained < limit) {
        eb_telemetry_record_t rec;
        if (!eb_storage_peek_oldest(storage, &rec)) {
            break;
        }
        int json_len = eb_mqtt_format_record_json(
            scratch_buf, scratch_cap, client->device_id, &rec, profiles);
        if (json_len <= 0) {
            client->publish_failures++;
            break;
        }
        if (publish_fn(user_ctx, topic, scratch_buf, (size_t)json_len) != 0) {
            client->publish_failures++;
            break;
        }
        (void)eb_storage_ack_oldest(storage);
        client->published_count++;
        client->last_publish_ms = rec.timestamp_ms;
        drained++;
    }
    return drained;
}
