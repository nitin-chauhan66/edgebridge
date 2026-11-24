/*
 * Copyright (c) 2026 Nitin Chauhan / EdgeBridge-S3
 * SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-EdgeBridge-Commercial
 */

#include "eb_profile.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *id;
    const char *json;
} builtin_profile_entry_t;

static const char BUILTIN_JK_BMS_JSON[] =
    "{"
    "\"profile_id\":\"jk_bms_modbus_v1\","
    "\"device_name\":\"JK-PB BMS (RS-485 Modbus RTU)\","
    "\"bus\":\"rs485\","
    "\"default_baud\":115200,"
    "\"slave_id\":1,"
    "\"poll_interval_ms\":1000,"
    "\"turnaround_guard_us\":2000,"
    "\"registers\":["
    "{\"name\":\"pack_voltage_v\",\"func\":3,\"address\":4608,\"length\":1,\"scale\":0.01,\"unit\":\"V\"},"
    "{\"name\":\"pack_current_a\",\"func\":3,\"address\":4609,\"length\":2,\"signed\":true,\"scale\":0.001,\"unit\":\"A\"},"
    "{\"name\":\"soc_percent\",\"func\":3,\"address\":4611,\"length\":1,\"scale\":1.0,\"unit\":\"%\"}"
    "]"
    "}";

static const char BUILTIN_PYLONTECH_CAN_JSON[] =
    "{"
    "\"profile_id\":\"pylontech_can_v1\","
    "\"device_name\":\"Pylontech/Victron CAN BMS\","
    "\"bus\":\"can\","
    "\"default_baud\":500000,"
    "\"slave_id\":1,"
    "\"poll_interval_ms\":1000,"
    "\"registers\":["
    "{\"name\":\"charge_voltage_limit_v\",\"can_id\":849,\"byte_offset\":0,\"length\":2,\"little_endian\":true,\"scale\":0.1,\"unit\":\"V\"},"
    "{\"name\":\"soc_percent\",\"can_id\":853,\"byte_offset\":0,\"length\":2,\"little_endian\":true,\"scale\":1.0,\"unit\":\"%\"},"
    "{\"name\":\"soh_percent\",\"can_id\":853,\"byte_offset\":2,\"length\":2,\"little_endian\":true,\"scale\":1.0,\"unit\":\"%\"},"
    "{\"name\":\"pack_voltage_v\",\"can_id\":854,\"byte_offset\":0,\"length\":2,\"little_endian\":true,\"scale\":0.01,\"unit\":\"V\"},"
    "{\"name\":\"pack_current_a\",\"can_id\":854,\"byte_offset\":2,\"length\":2,\"signed\":true,\"little_endian\":true,\"scale\":0.1,\"unit\":\"A\"},"
    "{\"name\":\"pack_temp_c\",\"can_id\":854,\"byte_offset\":4,\"length\":2,\"signed\":true,\"little_endian\":true,\"scale\":0.1,\"unit\":\"C\"}"
    "]"
    "}";

static const char BUILTIN_DEYE_INVERTER_JSON[] =
    "{"
    "\"profile_id\":\"deye_inverter_modbus_v1\","
    "\"device_name\":\"Deye/Sunsynk Hybrid Inverter\","
    "\"bus\":\"rs485\","
    "\"default_baud\":9600,"
    "\"slave_id\":1,"
    "\"poll_interval_ms\":1000,"
    "\"turnaround_guard_us\":500,"
    "\"registers\":["
    "{\"name\":\"battery_soc_pct\",\"func\":3,\"address\":588,\"length\":1,\"scale\":1.0,\"unit\":\"%\"},"
    "{\"name\":\"battery_power_w\",\"func\":3,\"address\":590,\"length\":1,\"signed\":true,\"scale\":1.0,\"unit\":\"W\"},"
    "{\"name\":\"grid_power_w\",\"func\":3,\"address\":625,\"length\":1,\"signed\":true,\"scale\":1.0,\"unit\":\"W\"},"
    "{\"name\":\"pv1_power_w\",\"func\":3,\"address\":672,\"length\":1,\"scale\":1.0,\"unit\":\"W\"}"
    "]"
    "}";

static const char BUILTIN_GROWATT_INVERTER_JSON[] =
    "{"
    "\"profile_id\":\"growatt_inverter_modbus_v1\","
    "\"device_name\":\"Growatt Inverter (Modbus 0x04)\","
    "\"bus\":\"rs485\","
    "\"default_baud\":9600,"
    "\"slave_id\":2,"
    "\"poll_interval_ms\":1000,"
    "\"turnaround_guard_us\":500,"
    "\"registers\":["
    "{\"name\":\"pv_input_power_w\",\"func\":4,\"address\":1,\"length\":2,\"scale\":0.1,\"unit\":\"W\"},"
    "{\"name\":\"grid_voltage_v\",\"func\":4,\"address\":38,\"length\":1,\"scale\":0.1,\"unit\":\"V\"},"
    "{\"name\":\"ac_output_power_w\",\"func\":4,\"address\":35,\"length\":2,\"scale\":0.1,\"unit\":\"W\"}"
    "]"
    "}";

static const builtin_profile_entry_t BUILTIN_PROFILES[] = {
    {"jk_bms_modbus_v1", BUILTIN_JK_BMS_JSON},
    {"pylontech_can_v1", BUILTIN_PYLONTECH_CAN_JSON},
    {"deye_inverter_modbus_v1", BUILTIN_DEYE_INVERTER_JSON},
    {"growatt_inverter_modbus_v1", BUILTIN_GROWATT_INVERTER_JSON},
};

/* --- Minimal zero-allocation JSON field extractors --- */

static const char *skip_ws(const char *p, const char *end) {
    while (p < end && isspace((unsigned char)*p)) {
        p++;
    }
    return p;
}

static const char *find_matching_brace(const char *open_p, const char *end, char open_ch, char close_ch) {
    int depth = 0;
    bool in_str = false;
    for (const char *p = open_p; p < end; p++) {
        if (*p == '"' && (p == open_p || *(p - 1) != '\\')) {
            in_str = !in_str;
            continue;
        }
        if (in_str) {
            continue;
        }
        if (*p == open_ch) {
            depth++;
        } else if (*p == close_ch) {
            depth--;
            if (depth == 0) {
                return p;
            }
        }
    }
    return NULL;
}

/* Searches only top-level keys of a JSON object [obj_start .. obj_end]. */
static const char *find_json_key(const char *obj_start, const char *obj_end, const char *key) {
    size_t key_len = strlen(key);
    const char *p = skip_ws(obj_start, obj_end);
    if (p >= obj_end || *p != '{') {
        return NULL;
    }
    p++;
    while (p < obj_end) {
        p = skip_ws(p, obj_end);
        if (p >= obj_end || *p == '}') {
            break;
        }
        if (*p == ',') {
            p++;
            continue;
        }
        if (*p != '"') {
            p++;
            continue;
        }
        const char *k_start = p + 1;
        const char *k_end = k_start;
        while (k_end < obj_end && *k_end != '"') {
            k_end++;
        }
        if (k_end >= obj_end) {
            break;
        }
        const char *after_k = skip_ws(k_end + 1, obj_end);
        if (after_k >= obj_end || *after_k != ':') {
            p = k_end + 1;
            continue;
        }
        const char *val = skip_ws(after_k + 1, obj_end);
        if ((size_t)(k_end - k_start) == key_len && strncmp(k_start, key, key_len) == 0) {
            return val;
        }
        /* Skip value cleanly including nested objects/arrays */
        if (val < obj_end && *val == '{') {
            const char *close_b = find_matching_brace(val, obj_end, '{', '}');
            p = close_b ? close_b + 1 : obj_end;
        } else if (val < obj_end && *val == '[') {
            const char *close_b = find_matching_brace(val, obj_end, '[', ']');
            p = close_b ? close_b + 1 : obj_end;
        } else if (val < obj_end && *val == '"') {
            p = val + 1;
            while (p < obj_end && !(*p == '"' && *(p - 1) != '\\')) {
                p++;
            }
            if (p < obj_end) {
                p++;
            }
        } else {
            p = val;
            while (p < obj_end && *p != ',' && *p != '}') {
                p++;
            }
        }
    }
    return NULL;
}

static bool extract_json_str(const char *obj_s, const char *obj_e, const char *key,
                             char *out, size_t out_cap) {
    const char *v = find_json_key(obj_s, obj_e, key);
    if (!v || v >= obj_e || *v != '"' || out_cap == 0U) {
        return false;
    }
    v++;
    size_t n = 0U;
    while (v < obj_e && *v != '"' && n + 1U < out_cap) {
        out[n++] = *v++;
    }
    out[n] = '\0';
    return true;
}

static bool extract_json_double(const char *obj_s, const char *obj_e, const char *key, double *out) {
    const char *v = find_json_key(obj_s, obj_e, key);
    if (!v || v >= obj_e) {
        return false;
    }
    char buf[32];
    size_t n = 0U;
    while (v < obj_e && n + 1U < sizeof(buf) &&
           (isdigit((unsigned char)*v) || *v == '-' || *v == '+' || *v == '.' || *v == 'e' || *v == 'E')) {
        buf[n++] = *v++;
    }
    if (n == 0U) {
        return false;
    }
    buf[n] = '\0';
    *out = strtod(buf, NULL);
    return true;
}

static bool extract_json_bool(const char *obj_s, const char *obj_e, const char *key, bool *out) {
    const char *v = find_json_key(obj_s, obj_e, key);
    if (!v || v >= obj_e) {
        return false;
    }
    if ((size_t)(obj_e - v) >= 4U && strncmp(v, "true", 4) == 0) {
        *out = true;
        return true;
    }
    if ((size_t)(obj_e - v) >= 5U && strncmp(v, "false", 5) == 0) {
        *out = false;
        return true;
    }
    return false;
}

void eb_profile_mgr_init(eb_profile_mgr_t *mgr) {
    if (!mgr) {
        return;
    }
    memset(mgr, 0, sizeof(*mgr));
}

void eb_profile_clear(eb_profile_mgr_t *mgr) {
    if (!mgr) {
        return;
    }
    memset(mgr, 0, sizeof(*mgr));
}

size_t eb_profile_builtin_count(void) {
    return sizeof(BUILTIN_PROFILES) / sizeof(BUILTIN_PROFILES[0]);
}

const char *eb_profile_builtin_id(size_t idx) {
    if (idx >= eb_profile_builtin_count()) {
        return NULL;
    }
    return BUILTIN_PROFILES[idx].id;
}

const char *eb_profile_builtin_json(size_t idx) {
    if (idx >= eb_profile_builtin_count()) {
        return NULL;
    }
    return BUILTIN_PROFILES[idx].json;
}

int eb_profile_load_json(eb_profile_mgr_t *mgr, const char *json_str, size_t json_len) {
    if (!mgr || !json_str || json_len == 0U) {
        return -1;
    }
    const char *end = json_str + json_len;
    const char *root = skip_ws(json_str, end);
    if (root >= end || *root != '{') {
        return -2;
    }
    const char *root_end = find_matching_brace(root, end, '{', '}');
    if (!root_end) {
        return -2;
    }
    root_end++;

    eb_device_profile_t prof;
    memset(&prof, 0, sizeof(prof));
    if (!extract_json_str(root, root_end, "profile_id", prof.profile_id, sizeof(prof.profile_id))) {
        return -3;
    }
    extract_json_str(root, root_end, "device_name", prof.device_name, sizeof(prof.device_name));

    char bus_str[16] = "rs485";
    extract_json_str(root, root_end, "bus", bus_str, sizeof(bus_str));
    prof.bus = (strcmp(bus_str, "can") == 0) ? EB_BUS_CAN : EB_BUS_RS485;

    double dval = 0.0;
    prof.baud_or_bitrate = extract_json_double(root, root_end, "default_baud", &dval)
                               ? (uint32_t)dval
                               : (prof.bus == EB_BUS_CAN ? 500000U : 9600U);
    prof.slave_id = extract_json_double(root, root_end, "slave_id", &dval) ? (uint8_t)dval : 1U;
    prof.poll_interval_ms = extract_json_double(root, root_end, "poll_interval_ms", &dval)
                                ? (uint16_t)dval
                                : 1000U;
    prof.turnaround_guard_us = extract_json_double(root, root_end, "turnaround_guard_us", &dval)
                                   ? (uint32_t)dval
                                   : 250U;

    const char *arr = find_json_key(root, root_end, "registers");
    if (!arr || *arr != '[') {
        return -4;
    }
    const char *arr_end = find_matching_brace(arr, root_end, '[', ']');
    if (!arr_end) {
        return -4;
    }

    const char *cur = arr + 1;
    while (cur < arr_end && prof.signal_count < EB_PROFILE_MAX_METRICS) {
        cur = skip_ws(cur, arr_end);
        if (cur >= arr_end || *cur == ']') {
            break;
        }
        if (*cur != '{') {
            cur++;
            continue;
        }
        const char *item_end = find_matching_brace(cur, arr_end, '{', '}');
        if (!item_end) {
            break;
        }
        item_end++;

        eb_signal_def_t sig;
        memset(&sig, 0, sizeof(sig));
        sig.func = 3U;
        sig.length = 1U;
        sig.scale = 1.0;
        if (extract_json_str(cur, item_end, "name", sig.name, sizeof(sig.name))) {
            extract_json_str(cur, item_end, "unit", sig.unit, sizeof(sig.unit));
            if (extract_json_double(cur, item_end, "func", &dval)) sig.func = (uint8_t)dval;
            if (extract_json_double(cur, item_end, "address", &dval)) sig.address = (uint16_t)dval;
            if (extract_json_double(cur, item_end, "length", &dval)) sig.length = (uint8_t)dval;
            if (extract_json_double(cur, item_end, "slave_id", &dval)) sig.slave_id = (uint8_t)dval;
            if (extract_json_double(cur, item_end, "can_id", &dval)) sig.can_id = (uint32_t)dval;
            if (extract_json_double(cur, item_end, "byte_offset", &dval)) sig.byte_offset = (uint8_t)dval;
            if (extract_json_double(cur, item_end, "scale", &dval)) sig.scale = dval;
            extract_json_bool(cur, item_end, "signed", &sig.is_signed);
            extract_json_bool(cur, item_end, "little_endian", &sig.little_endian);
            prof.signals[prof.signal_count++] = sig;
        }
        cur = item_end;
    }

    if (prof.signal_count == 0U) {
        return -5;
    }

    /* Replace existing profile with same ID or append */
    for (size_t i = 0U; i < mgr->profile_count; i++) {
        if (strcmp(mgr->active_profiles[i].profile_id, prof.profile_id) == 0 &&
            mgr->active_profiles[i].slave_id == prof.slave_id) {
            mgr->active_profiles[i] = prof;
            return 0;
        }
    }
    if (mgr->profile_count >= EB_PROFILE_MAX_ACTIVE) {
        return -6;
    }
    mgr->active_profiles[mgr->profile_count++] = prof;
    return 0;
}

int eb_profile_load_builtin(eb_profile_mgr_t *mgr, const char *profile_id, uint8_t slave_override) {
    if (!mgr || !profile_id) {
        return -1;
    }
    for (size_t i = 0U; i < eb_profile_builtin_count(); i++) {
        if (strcmp(BUILTIN_PROFILES[i].id, profile_id) == 0) {
            int rc = eb_profile_load_json(mgr, BUILTIN_PROFILES[i].json,
                                          strlen(BUILTIN_PROFILES[i].json));
            if (rc == 0 && slave_override > 0U && mgr->profile_count > 0U) {
                mgr->active_profiles[mgr->profile_count - 1U].slave_id = slave_override;
            }
            return rc;
        }
    }
    return -1;
}

static void upsert_snapshot(eb_profile_mgr_t *mgr, const eb_decoded_value_t *val) {
    for (size_t i = 0U; i < mgr->latest_count; i++) {
        eb_decoded_value_t *cur = &mgr->latest[i];
        if (cur->bus == val->bus && cur->addr == val->addr &&
            cur->can_id == val->can_id &&
            strcmp(cur->metric_name, val->metric_name) == 0) {
            *cur = *val;
            return;
        }
    }
    if (mgr->latest_count < EB_SNAPSHOT_MAX_VALUES) {
        mgr->latest[mgr->latest_count++] = *val;
    }
}

int eb_profile_decode_modbus_signal(eb_profile_mgr_t *mgr,
                                    const eb_device_profile_t *prof,
                                    const eb_signal_def_t *sig,
                                    const uint8_t *reg_bytes,
                                    size_t byte_len,
                                    uint64_t timestamp_ms,
                                    eb_decoded_value_t *out_val) {
    if (!prof || !sig || !reg_bytes) {
        return -1;
    }
    size_t needed = (size_t)sig->length * 2U;
    if (needed == 0U || byte_len < needed) {
        return -2;
    }

    double raw = 0.0;
    if (sig->length == 1U) {
        uint16_t u16 = ((uint16_t)reg_bytes[0] << 8) | reg_bytes[1];
        raw = sig->is_signed ? (double)(int16_t)u16 : (double)u16;
    } else if (sig->length == 2U) {
        uint32_t u32 = ((uint32_t)reg_bytes[0] << 24) | ((uint32_t)reg_bytes[1] << 16) |
                       ((uint32_t)reg_bytes[2] << 8) | reg_bytes[3];
        raw = sig->is_signed ? (double)(int32_t)u32 : (double)u32;
    } else {
        return -3;
    }

    eb_decoded_value_t decoded;
    memset(&decoded, 0, sizeof(decoded));
    snprintf(decoded.profile_id, sizeof(decoded.profile_id), "%s", prof->profile_id);
    snprintf(decoded.metric_name, sizeof(decoded.metric_name), "%s", sig->name);
    snprintf(decoded.unit, sizeof(decoded.unit), "%s", sig->unit);
    decoded.bus = EB_BUS_RS485;
    decoded.addr = (sig->slave_id > 0U) ? sig->slave_id : prof->slave_id;
    decoded.value = raw * sig->scale;
    decoded.timestamp_ms = timestamp_ms;
    decoded.valid = true;

    if (mgr) {
        upsert_snapshot(mgr, &decoded);
    }
    if (out_val) {
        *out_val = decoded;
    }
    return 0;
}

size_t eb_profile_decode_can_frame(eb_profile_mgr_t *mgr,
                                   const eb_can_frame_t *frame,
                                   uint64_t timestamp_ms) {
    if (!mgr || !frame) {
        return 0U;
    }
    size_t decoded_n = 0U;

    for (size_t pi = 0U; pi < mgr->profile_count; pi++) {
        const eb_device_profile_t *prof = &mgr->active_profiles[pi];
        if (prof->bus != EB_BUS_CAN) {
            continue;
        }
        for (size_t si = 0U; si < prof->signal_count; si++) {
            const eb_signal_def_t *sig = &prof->signals[si];
            if (sig->can_id != frame->id) {
                continue;
            }
            if ((size_t)sig->byte_offset + sig->length > frame->dlc ||
                sig->byte_offset + sig->length > 8U) {
                continue;
            }
            const uint8_t *d = &frame->data[sig->byte_offset];
            double raw = 0.0;
            if (sig->length == 1U) {
                raw = sig->is_signed ? (double)(int8_t)d[0] : (double)d[0];
            } else if (sig->length == 2U) {
                uint16_t u16 = sig->little_endian
                                   ? ((uint16_t)d[0] | ((uint16_t)d[1] << 8))
                                   : (((uint16_t)d[0] << 8) | d[1]);
                raw = sig->is_signed ? (double)(int16_t)u16 : (double)u16;
            } else if (sig->length == 4U) {
                uint32_t u32 = sig->little_endian
                                   ? ((uint32_t)d[0] | ((uint32_t)d[1] << 8) |
                                      ((uint32_t)d[2] << 16) | ((uint32_t)d[3] << 24))
                                   : (((uint32_t)d[0] << 24) | ((uint32_t)d[1] << 16) |
                                      ((uint32_t)d[2] << 8) | d[3]);
                raw = sig->is_signed ? (double)(int32_t)u32 : (double)u32;
            } else {
                continue;
            }

            eb_decoded_value_t decoded;
            memset(&decoded, 0, sizeof(decoded));
            snprintf(decoded.profile_id, sizeof(decoded.profile_id), "%s", prof->profile_id);
            snprintf(decoded.metric_name, sizeof(decoded.metric_name), "%s", sig->name);
            snprintf(decoded.unit, sizeof(decoded.unit), "%s", sig->unit);
            decoded.bus = EB_BUS_CAN;
            decoded.addr = prof->slave_id;
            decoded.can_id = frame->id;
            decoded.value = raw * sig->scale;
            decoded.timestamp_ms = timestamp_ms;
            decoded.valid = true;

            upsert_snapshot(mgr, &decoded);
            decoded_n++;
        }
    }
    return decoded_n;
}
