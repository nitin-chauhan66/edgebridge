/*
 * Copyright (c) 2026 Nitin Chauhan / EdgeBridge-S3
 * SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-EdgeBridge-Commercial
 */

#include "eb_web_analyzer.h"
#include "eb_version.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t get_now_sec(const eb_web_ctx_t *ctx) {
    if (ctx && ctx->now_sec_cb) {
        return ctx->now_sec_cb(ctx->user_ctx);
    }
    return 0U;
}

static bool extract_simple_str(const char *json, size_t len, const char *key, char *out, size_t cap) {
    if (!json || !key || !out || cap == 0U) {
        return false;
    }
    char pattern[48];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *p = strstr(json, pattern);
    if (!p || (size_t)(p - json) >= len) {
        return false;
    }
    p += strlen(pattern);
    const char *end = json + len;
    while (p < end && (*p == ' ' || *p == '\t' || *p == ':')) {
        p++;
    }
    if (p >= end || *p != '"') {
        return false;
    }
    p++;
    size_t n = 0U;
    while (p < end && *p != '"' && n + 1U < cap) {
        out[n++] = *p++;
    }
    out[n] = '\0';
    return true;
}

static bool extract_simple_u32(const char *json, size_t len, const char *key, uint32_t *out) {
    if (!json || !key || !out) {
        return false;
    }
    char pattern[48];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *p = strstr(json, pattern);
    if (!p || (size_t)(p - json) >= len) {
        return false;
    }
    p += strlen(pattern);
    const char *end = json + len;
    while (p < end && (*p == ' ' || *p == '\t' || *p == ':')) {
        p++;
    }
    if (p >= end || !isdigit((unsigned char)*p)) {
        return false;
    }
    *out = (uint32_t)strtoul(p, NULL, 10);
    return true;
}

static bool extract_simple_bool(const char *json, size_t len, const char *key, bool *out) {
    if (!json || !key || !out) {
        return false;
    }
    char pattern[48];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *p = strstr(json, pattern);
    if (!p || (size_t)(p - json) >= len) {
        return false;
    }
    p += strlen(pattern);
    const char *end = json + len;
    while (p < end && (*p == ' ' || *p == '\t' || *p == ':')) {
        p++;
    }
    if ((size_t)(end - p) >= 4U && strncmp(p, "true", 4) == 0) {
        *out = true;
        return true;
    }
    if ((size_t)(end - p) >= 5U && strncmp(p, "false", 5) == 0) {
        *out = false;
        return true;
    }
    return false;
}

int eb_web_format_diagnostics_json(char *out_buf, size_t buf_len,
                                   const eb_rs485_ctx_t *rs485,
                                   const eb_can_diag_t *can,
                                   const eb_storage_ring_t *storage,
                                   const eb_config_mgr_t *cfg,
                                   const eb_ble_prov_t *ble) {
    if (!out_buf || buf_len == 0U || !rs485 || !can || !storage || !cfg || !ble) {
        return -1;
    }

    return snprintf(
        out_buf, buf_len,
        "{"
        "\"rs485\":{\"baud\":%u,\"t35_us\":%u,\"adaptive_us\":%u,\"auto_dir\":%s,\"ceiling_warn\":%s,"
        "\"tx\":%u,\"rx\":%u,\"crc_err\":%u,\"clipped_err\":%u,\"timeout_err\":%u,\"exc_err\":%u},"
        "\"can\":{\"bitrate\":%u,\"state\":\"%s\",\"tec\":%u,\"rec\":%u,"
        "\"tx\":%u,\"rx\":%u,\"ack_ok\":%u,\"ack_miss\":%u,\"bus_off\":%u,\"peer_ack\":%s,"
        "\"hint\":\"%s\"},"
        "\"storage\":{\"buffered\":%zu,\"capacity\":%zu,\"overwritten\":%u,\"corrupt_skipped\":%u},"
        "\"config\":{\"version\":%u,\"pending_confirm\":%s,\"rollbacks\":%u,\"profile\":\"%s\"},"
        "\"ble\":{\"radio_active\":%s,\"ap_fallback\":%s}"
        "}",
        rs485->baud_rate,
        rs485->effective_t35_us,
        rs485->adaptive_extra_us,
        rs485->auto_direction ? "true" : "false",
        rs485->baud_ceiling_warn ? "true" : "false",
        rs485->tx_frames,
        rs485->rx_frames,
        rs485->crc_errors,
        rs485->framing_clipped_errors,
        rs485->timeout_errors,
        rs485->exception_responses,
        can->bitrate,
        eb_can_state_str(can->state),
        (unsigned)can->tec,
        (unsigned)can->rec,
        can->tx_frames,
        can->rx_frames,
        can->ack_ok_count,
        can->ack_missing_count,
        can->bus_off_count,
        can->peer_ack_healthy ? "true" : "false",
        eb_can_health_hint(can),
        storage->count,
        storage->capacity,
        storage->overwritten_count,
        storage->corrupt_skipped_count,
        cfg->active.version,
        cfg->pending_confirm ? "true" : "false",
        cfg->rollback_count,
        cfg->active.active_profile,
        ble->radio_active ? "true" : "false",
        ble->ap_fallback_active ? "true" : "false");
}

int eb_web_format_full_diagnostics_json(char *out_buf, size_t buf_len,
                                        const eb_web_ctx_t *ctx) {
    if (!ctx || !ctx->rs485 || !ctx->can || !ctx->storage || !ctx->cfg || !ctx->ble) {
        return -1;
    }
    int base = eb_web_format_diagnostics_json(out_buf, buf_len,
                                              ctx->rs485, ctx->can,
                                              ctx->storage, ctx->cfg, ctx->ble);
    if (base <= 1 || (size_t)base >= buf_len) {
        return -1;
    }
    /* Replace trailing '}' with extended MQTT, OTA, Memory & Watchdog objects */
    size_t pos = (size_t)base - 1U;
    uint32_t rem = eb_config_remaining_watchdog_sec(ctx->cfg, get_now_sec(ctx));
    uint32_t sram_pct = ctx->mem ? eb_mem_internal_sram_pct(ctx->mem) : 0U;

    int ext = snprintf(
        out_buf + pos, buf_len - pos,
        ",\"fw_version\":\"%s\",\"watchdog_remaining_sec\":%u,"
        "\"mqtt\":{\"wifi_up\":%s,\"connected\":%s,\"published\":%u,\"failures\":%u,\"outages\":%u},"
        "\"ota\":{\"confirmed\":%s,\"net_ok\":%s,\"bus_ok\":%s,\"status\":\"%s\",\"bytes\":%zu,\"total\":%zu},"
        "\"memory\":{\"internal_sram_pct\":%u,\"psram_active\":%s,\"psram_total\":%u,\"psram_used\":%u}"
        "}",
        EB_FW_VERSION_STR,
        rem,
        (ctx->mqtt && ctx->mqtt->wifi_connected) ? "true" : "false",
        (ctx->mqtt && ctx->mqtt->mqtt_connected) ? "true" : "false",
        ctx->mqtt ? ctx->mqtt->published_count : 0U,
        ctx->mqtt ? ctx->mqtt->publish_failures : 0U,
        ctx->mqtt ? ctx->mqtt->outage_count : 0U,
        (ctx->ota && ctx->ota->image_confirmed) ? "true" : "false",
        (ctx->ota && ctx->ota->network_healthy) ? "true" : "false",
        (ctx->ota && ctx->ota->bus_healthy) ? "true" : "false",
        ctx->ota ? eb_ota_status_str(ctx->ota->dl_status) : "IDLE",
        ctx->ota ? ctx->ota->bytes_received : 0U,
        ctx->ota ? ctx->ota->expected_size : 0U,
        sram_pct,
        (ctx->mem && ctx->mem->psram_active) ? "true" : "false",
        ctx->mem ? ctx->mem->psram_total_bytes : 0U,
        ctx->mem ? ctx->mem->psram_used_bytes : 0U);
    if (ext < 0 || pos + (size_t)ext >= buf_len) {
        return -1;
    }
    return (int)(pos + (size_t)ext);
}

int eb_web_format_telemetry_json(char *out_buf, size_t buf_len, const eb_web_ctx_t *ctx) {
    if (!out_buf || buf_len == 0U || !ctx) {
        return -1;
    }
    int pos = snprintf(out_buf, buf_len, "{\"metrics\":[");
    if (pos < 0 || (size_t)pos >= buf_len) {
        return -1;
    }

    if (ctx->profiles) {
        bool first = true;
        for (size_t i = 0U; i < ctx->profiles->latest_count; i++) {
            const eb_decoded_value_t *v = &ctx->profiles->latest[i];
            if (!v->valid) {
                continue;
            }
            int n = snprintf(
                out_buf + pos, buf_len - (size_t)pos,
                "%s{\"profile\":\"%s\",\"name\":\"%s\",\"value\":%.3f,\"unit\":\"%s\","
                "\"bus\":\"%s\",\"addr\":%u,\"can_id\":%u,\"ts_ms\":%llu}",
                first ? "" : ",",
                v->profile_id, v->metric_name, v->value, v->unit,
                (v->bus == EB_BUS_CAN) ? "can" : "rs485",
                (unsigned)v->addr, v->can_id, (unsigned long long)v->timestamp_ms);
            if (n < 0 || (size_t)(pos + n) >= buf_len) {
                return -1;
            }
            pos += n;
            first = false;
        }
    }

    int n = snprintf(out_buf + pos, buf_len - (size_t)pos, "],\"rs485_frames\":[");
    if (n < 0 || (size_t)(pos + n) >= buf_len) {
        return -1;
    }
    pos += n;

    if (ctx->rs485 && ctx->rs485->recent_count > 0U) {
        size_t cnt = ctx->rs485->recent_count;
        size_t start = (ctx->rs485->recent_head + EB_RS485_FRAME_LOG_CAP - cnt) %
                       EB_RS485_FRAME_LOG_CAP;
        for (size_t i = 0U; i < cnt; i++) {
            const eb_rs485_frame_log_t *f =
                &ctx->rs485->recent_frames[(start + i) % EB_RS485_FRAME_LOG_CAP];
            n = snprintf(out_buf + pos, buf_len - (size_t)pos,
                         "%s{\"ts_ms\":%llu,\"dir\":\"%s\",\"slave\":%u,\"func\":%u,"
                         "\"len\":%u,\"status\":%d}",
                         (i == 0U) ? "" : ",",
                         (unsigned long long)f->timestamp_ms,
                         f->is_tx ? "TX" : "RX",
                         (unsigned)f->slave_id, (unsigned)f->func_code,
                         (unsigned)f->len, (int)f->status);
            if (n < 0 || (size_t)(pos + n) >= buf_len) {
                return -1;
            }
            pos += n;
        }
    }

    n = snprintf(out_buf + pos, buf_len - (size_t)pos, "],\"can_frames\":[");
    if (n < 0 || (size_t)(pos + n) >= buf_len) {
        return -1;
    }
    pos += n;

    if (ctx->can && ctx->can->recent_count > 0U) {
        size_t cnt = ctx->can->recent_count;
        size_t start = (ctx->can->recent_head + EB_CAN_FRAME_LOG_CAP - cnt) %
                       EB_CAN_FRAME_LOG_CAP;
        for (size_t i = 0U; i < cnt; i++) {
            const eb_can_frame_t *f =
                &ctx->can->recent_frames[(start + i) % EB_CAN_FRAME_LOG_CAP];
            n = snprintf(out_buf + pos, buf_len - (size_t)pos,
                         "%s{\"ts_ms\":%llu,\"id\":%u,\"dlc\":%u}",
                         (i == 0U) ? "" : ",",
                         (unsigned long long)f->timestamp_ms,
                         f->id, (unsigned)f->dlc);
            if (n < 0 || (size_t)(pos + n) >= buf_len) {
                return -1;
            }
            pos += n;
        }
    }

    n = snprintf(out_buf + pos, buf_len - (size_t)pos, "]}");
    if (n < 0 || (size_t)(pos + n) >= buf_len) {
        return -1;
    }
    return pos + n;
}

static int format_scan_json(char *out_buf, size_t buf_len, const eb_scan_result_t *scan) {
    if (!scan) {
        return snprintf(out_buf, buf_len, "{\"scan_completed\":false,\"hits\":[],\"can_ids\":[]}");
    }
    int pos = snprintf(out_buf, buf_len,
                       "{\"scan_completed\":%s,\"bauds_probed\":%u,\"probes_sent\":%u,\"hits\":[",
                       scan->scan_completed ? "true" : "false",
                       scan->bauds_probed, scan->probes_sent);
    if (pos < 0 || (size_t)pos >= buf_len) {
        return -1;
    }
    for (size_t i = 0U; i < scan->hit_count; i++) {
        const eb_scan_rs485_hit_t *h = &scan->hits[i];
        int n = snprintf(out_buf + pos, buf_len - (size_t)pos,
                         "%s{\"baud\":%u,\"slave_id\":%u,\"reg\":%u,\"exception\":%s}",
                         (i == 0U) ? "" : ",",
                         h->baud_rate, (unsigned)h->slave_id, (unsigned)h->probe_reg,
                         h->is_exception ? "true" : "false");
        if (n < 0 || (size_t)(pos + n) >= buf_len) {
            return -1;
        }
        pos += n;
    }
    int n = snprintf(out_buf + pos, buf_len - (size_t)pos,
                     "],\"can_bitrate\":%u,\"can_ids\":[", scan->detected_can_bitrate);
    if (n < 0 || (size_t)(pos + n) >= buf_len) {
        return -1;
    }
    pos += n;
    for (size_t i = 0U; i < scan->can_id_count; i++) {
        n = snprintf(out_buf + pos, buf_len - (size_t)pos, "%s%u",
                     (i == 0U) ? "" : ",", scan->can_ids[i]);
        if (n < 0 || (size_t)(pos + n) >= buf_len) {
            return -1;
        }
        pos += n;
    }
    n = snprintf(out_buf + pos, buf_len - (size_t)pos, "]}");
    return (n < 0 || (size_t)(pos + n) >= buf_len) ? -1 : (pos + n);
}

int eb_web_handle_http(eb_web_ctx_t *ctx,
                       const char *method,
                       const char *path,
                       const char *body,
                       size_t body_len,
                       int *out_status,
                       const char **out_content_type,
                       char *out_buf,
                       size_t out_cap) {
    if (!ctx || !method || !path || !out_status || !out_content_type || !out_buf || out_cap == 0U) {
        return -1;
    }

    if (strcmp(method, "GET") == 0 && strcmp(path, "/") == 0) {
        *out_status = 200;
        *out_content_type = "text/html; charset=utf-8";
        return snprintf(out_buf, out_cap, "%s", eb_web_get_analyzer_html());
    }

    *out_content_type = "application/json";

    if (strcmp(method, "GET") == 0 && strcmp(path, "/api/v1/diagnostics") == 0) {
        *out_status = 200;
        return eb_web_format_full_diagnostics_json(out_buf, out_cap, ctx);
    }

    if (strcmp(method, "GET") == 0 && strcmp(path, "/api/v1/telemetry") == 0) {
        *out_status = 200;
        return eb_web_format_telemetry_json(out_buf, out_cap, ctx);
    }

    if (strcmp(path, "/api/v1/scan") == 0) {
        if (strcmp(method, "POST") == 0 && ctx->trigger_scan_cb) {
            (void)ctx->trigger_scan_cb(ctx->user_ctx);
        }
        *out_status = 200;
        return format_scan_json(out_buf, out_cap, ctx->scan);
    }

    if (strcmp(path, "/api/v1/profiles") == 0) {
        if (strcmp(method, "POST") == 0 && ctx->profiles && body && body_len > 0U) {
            char builtin_id[32];
            uint32_t slave_ov = 0U;
            (void)extract_simple_u32(body, body_len, "slave_id", &slave_ov);
            int rc = -1;
            if (extract_simple_str(body, body_len, "builtin", builtin_id, sizeof(builtin_id))) {
                rc = eb_profile_load_builtin(ctx->profiles, builtin_id, (uint8_t)slave_ov);
                if (rc == 0 && ctx->cfg) {
                    eb_config_t next = ctx->cfg->active;
                    snprintf(next.active_profile, sizeof(next.active_profile), "%s", builtin_id);
                    (void)eb_config_stage(ctx->cfg, &next, get_now_sec(ctx));
                }
            } else {
                rc = eb_profile_load_json(ctx->profiles, body, body_len);
            }
            if (rc != 0) {
                *out_status = 400;
                return snprintf(out_buf, out_cap, "{\"error\":\"invalid_profile\",\"rc\":%d}", rc);
            }
        }
        *out_status = 200;
        int pos = snprintf(out_buf, out_cap, "{\"builtin\":[");
        for (size_t i = 0U; i < eb_profile_builtin_count(); i++) {
            int n = snprintf(out_buf + pos, out_cap - (size_t)pos, "%s\"%s\"",
                             (i == 0U) ? "" : ",", eb_profile_builtin_id(i));
            if (n < 0 || (size_t)(pos + n) >= out_cap) return -1;
            pos += n;
        }
        int n = snprintf(out_buf + pos, out_cap - (size_t)pos, "],\"active\":[");
        if (n < 0 || (size_t)(pos + n) >= out_cap) return -1;
        pos += n;
        if (ctx->profiles) {
            for (size_t i = 0U; i < ctx->profiles->profile_count; i++) {
                const eb_device_profile_t *p = &ctx->profiles->active_profiles[i];
                n = snprintf(out_buf + pos, out_cap - (size_t)pos,
                             "%s{\"id\":\"%s\",\"bus\":\"%s\",\"baud\":%u,\"slave_id\":%u}",
                             (i == 0U) ? "" : ",",
                             p->profile_id, (p->bus == EB_BUS_CAN) ? "can" : "rs485",
                             p->baud_or_bitrate, (unsigned)p->slave_id);
                if (n < 0 || (size_t)(pos + n) >= out_cap) return -1;
                pos += n;
            }
        }
        n = snprintf(out_buf + pos, out_cap - (size_t)pos, "]}");
        return (n < 0 || (size_t)(pos + n) >= out_cap) ? -1 : (pos + n);
    }

    if (strcmp(path, "/api/v1/config/confirm") == 0 && strcmp(method, "POST") == 0) {
        if (!ctx->cfg || eb_config_confirm(ctx->cfg) != 0) {
            *out_status = 400;
            return snprintf(out_buf, out_cap, "{\"confirmed\":false}");
        }
        *out_status = 200;
        return snprintf(out_buf, out_cap, "{\"confirmed\":true,\"version\":%u}",
                        ctx->cfg->active.version);
    }

    if (strcmp(path, "/api/v1/config") == 0 && ctx->cfg) {
        if (strcmp(method, "POST") == 0 && body && body_len > 0U) {
            eb_config_t next = ctx->cfg->active;
            uint32_t u32 = 0U;
            bool bval = false;
            if (extract_simple_u32(body, body_len, "rs485_baud", &u32)) next.rs485_baud = u32;
            if (extract_simple_u32(body, body_len, "guard_extra_us", &u32)) next.rs485_guard_extra_us = u32;
            if (extract_simple_bool(body, body_len, "rs485_auto_dir", &bval)) next.rs485_auto_direction = bval;
            if (extract_simple_u32(body, body_len, "can_bitrate", &u32)) next.can_bitrate = u32;
            if (extract_simple_u32(body, body_len, "poll_interval_ms", &u32)) next.poll_interval_ms = (uint16_t)u32;
            if (extract_simple_str(body, body_len, "wifi_ssid", next.wifi_ssid, sizeof(next.wifi_ssid))) {
                next.provisioned = true;
            }
            (void)extract_simple_str(body, body_len, "wifi_pass", next.wifi_pass, sizeof(next.wifi_pass));
            (void)extract_simple_str(body, body_len, "mqtt_broker", next.mqtt_broker, sizeof(next.mqtt_broker));
            if (extract_simple_u32(body, body_len, "mqtt_port", &u32)) next.mqtt_port = (uint16_t)u32;

            int rc = eb_config_stage(ctx->cfg, &next, get_now_sec(ctx));
            if (rc != 0) {
                *out_status = 400;
                return snprintf(out_buf, out_cap, "{\"error\":\"invalid_config\",\"rc\":%d}", rc);
            }
            if (ctx->rs485) {
                eb_rs485_init(ctx->rs485, next.rs485_baud, next.rs485_guard_extra_us,
                              next.rs485_auto_direction, next.rs485_strict_baud_cap);
            }
            if (ctx->ble && next.provisioned && ctx->ble->radio_active) {
                eb_ble_prov_notify_provisioned(ctx->ble);
            }
        }
        *out_status = 200;
        return snprintf(
            out_buf, out_cap,
            "{\"version\":%u,\"pending_confirm\":%s,\"watchdog_sec\":%u,"
            "\"rs485_baud\":%u,\"can_bitrate\":%u,\"poll_interval_ms\":%u,"
            "\"wifi_ssid\":\"%s\",\"mqtt_broker\":\"%s\",\"mqtt_port\":%u}",
            ctx->cfg->active.version,
            ctx->cfg->pending_confirm ? "true" : "false",
            eb_config_remaining_watchdog_sec(ctx->cfg, get_now_sec(ctx)),
            ctx->cfg->active.rs485_baud,
            ctx->cfg->active.can_bitrate,
            (unsigned)ctx->cfg->active.poll_interval_ms,
            ctx->cfg->active.wifi_ssid,
            ctx->cfg->active.mqtt_broker,
            (unsigned)ctx->cfg->active.mqtt_port);
    }

    if (strcmp(path, "/api/v1/ota") == 0 && ctx->ota) {
        *out_status = 200;
        return snprintf(out_buf, out_cap,
                        "{\"confirmed\":%s,\"status\":\"%s\",\"bytes\":%zu,\"expected\":%zu}",
                        ctx->ota->image_confirmed ? "true" : "false",
                        eb_ota_status_str(ctx->ota->dl_status),
                        ctx->ota->bytes_received, ctx->ota->expected_size);
    }

    *out_status = 404;
    return snprintf(out_buf, out_cap, "{\"error\":\"not_found\"}");
}

static const char EB_ANALYZER_HTML[] =
    "<!doctype html><html><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>EdgeBridge-S3 Bus Analyzer</title>"
    "<style>"
    "*{box-sizing:border-box}body{font-family:system-ui,-apple-system,sans-serif;background:#0b1120;color:#e2e8f0;margin:0;padding:12px;max-width:860px;margin:auto}"
    ".hdr{display:flex;justify-content:space-between;align-items:center;margin-bottom:12px}"
    "h1{font-size:1.15rem;margin:0;color:#f8fafc}h2{font-size:.95rem;margin:0 0 8px;color:#38bdf8}"
    ".grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(250px,1fr));gap:10px}"
    ".card{background:#162032;border:1px solid #23324d;border-radius:10px;padding:12px;margin-bottom:10px}"
    ".badge{display:inline-block;padding:2px 8px;border-radius:999px;font-size:.75rem;font-weight:600}"
    ".ok{background:#064e3b;color:#34d399}.warn{background:#78350f;color:#fbbf24}.err{background:#7f1d1d;color:#fca5a5}"
    "button,input,select{background:#0f172a;color:#e2e8f0;border:1px solid #334155;border-radius:6px;padding:7px 10px;font-size:.85rem;margin:3px 0}"
    "button{background:#0284c7;border-color:#0284c7;font-weight:600;cursor:pointer}button.sec{background:#334155;border-color:#475569}"
    ".row{display:flex;gap:8px;flex-wrap:wrap;align-items:center}"
    "table{width:100%;border-collapse:collapse;font-size:.82rem}th,td{text-align:left;padding:5px;border-bottom:1px solid #23324d}"
    "pre{margin:0;font-size:.78rem;white-space:pre-wrap;color:#94a3b8}"
    "</style></head><body>"
    "<div class='hdr'><h1>EdgeBridge-S3 Phase 0 Bus Analyzer</h1><span id='sram' class='badge ok'>SRAM --%</span></div>"
    "<div id='wdBanner' class='card' style='display:none;border-color:#f59e0b'>"
    "<strong>Config Watchdog Active:</strong> <span id='wdSec'>60</span>s until auto-rollback. "
    "<button onclick='confirmCfg()'>Confirm Config Now</button></div>"
    "<div class='grid'>"
    "<div class='card'><h2>RS-485 Health</h2><div id='rsHealth'>Loading...</div></div>"
    "<div class='card'><h2>CAN 2.0B (ACK / Error Analyzer)</h2><div id='canHealth'>Loading...</div></div>"
    "<div class='card'><h2>LittleFS Store &amp; MQTT</h2><div id='storeHealth'>Loading...</div></div>"
    "</div>"
    "<div class='card'><h2>5-Minute Commissioning &amp; Bus Auto-Scan</h2>"
    "<div class='row'><button onclick='runScan()'>Auto-Scan RS-485 &amp; CAN</button>"
    "<select id='profSel'><option value='jk_bms_modbus_v1'>jk_bms_modbus_v1</option>"
    "<option value='pylontech_can_v1'>pylontech_can_v1</option>"
    "<option value='deye_inverter_modbus_v1'>deye_inverter_modbus_v1</option>"
    "<option value='growatt_inverter_modbus_v1'>growatt_inverter_modbus_v1</option></select>"
    "<button class='sec' onclick='applyProfile()'>Load Profile</button></div>"
    "<pre id='scanOut'>No scan run yet.</pre></div>"
    "<div class='card'><h2>Wi-Fi &amp; MQTT Setup</h2>"
    "<div class='row'><input id='ssid' placeholder='Wi-Fi SSID'><input id='pass' type='password' placeholder='Password'>"
    "<input id='broker' placeholder='MQTT Broker'><input id='port' type='number' value='1883' style='width:80px'>"
    "<button onclick='saveNet()'>Stage &amp; Test (60s)</button></div></div>"
    "<div class='card'><h2>Live Decoded Telemetry</h2><table><thead><tr><th>Metric</th><th>Value</th><th>Bus</th></tr></thead>"
    "<tbody id='metrics'></tbody></table></div>"
    "<script>"
    "async function refresh(){"
    "const d=await (await fetch('/api/v1/diagnostics')).json();"
    "document.getElementById('sram').textContent='SRAM '+d.memory.internal_sram_pct+'% | PSRAM '+(d.memory.psram_active?'ON':'OFF');"
    "document.getElementById('wdBanner').style.display=d.config.pending_confirm?'block':'none';"
    "document.getElementById('wdSec').textContent=d.watchdog_remaining_sec;"
    "document.getElementById('rsHealth').innerHTML='Baud: <b>'+d.rs485.baud+'</b> (t3.5: '+d.rs485.t35_us+'us)<br>TX/RX: '+d.rs485.tx+'/'+d.rs485.rx+'<br>CRC/Clipped/TO: '+d.rs485.crc_err+'/'+d.rs485.clipped_err+'/'+d.rs485.timeout_err;"
    "const cb=d.can.peer_ack?'ok':'err';"
    "document.getElementById('canHealth').innerHTML=\"<span class='badge \"+cb+\"'>\"+d.can.state+'</span> Bitrate: '+d.can.bitrate+'<br>ACK OK/Miss: '+d.can.ack_ok+'/'+d.can.ack_miss+' | TEC/REC: '+d.can.tec+'/'+d.can.rec+'<br><small>'+d.can.hint+'</small>';"
    "document.getElementById('storeHealth').innerHTML='Buffered: '+d.storage.buffered+'/'+d.storage.capacity+' (Corrupt skipped: '+d.storage.corrupt_skipped+')<br>MQTT: '+(d.mqtt.connected?'Connected':'Offline')+' | Pub: '+d.mqtt.published;"
    "const t=await (await fetch('/api/v1/telemetry')).json();"
    "document.getElementById('metrics').innerHTML=t.metrics.map(m=>'<tr><td>'+m.name+'</td><td><b>'+m.value.toFixed(2)+'</b> '+m.unit+'</td><td>'+m.bus+'</td></tr>').join('');"
    "}"
    "async function runScan(){const r=await (await fetch('/api/v1/scan',{method:'POST'})).json();document.getElementById('scanOut').textContent=JSON.stringify(r,null,2);}"
    "async function applyProfile(){await fetch('/api/v1/profiles',{method:'POST',body:JSON.stringify({builtin:document.getElementById('profSel').value})});refresh();}"
    "async function saveNet(){await fetch('/api/v1/config',{method:'POST',body:JSON.stringify({wifi_ssid:ssid.value,wifi_pass:pass.value,mqtt_broker:broker.value,mqtt_port:+port.value})});refresh();}"
    "async function confirmCfg(){await fetch('/api/v1/config/confirm',{method:'POST'});refresh();}"
    "refresh();setInterval(refresh,1000);"
    "</script></body></html>";

const char *eb_web_get_analyzer_html(void) {
    return EB_ANALYZER_HTML;
}
