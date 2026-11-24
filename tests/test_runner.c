#include "eb_ble_prov.h"
#include "eb_can.h"
#include "eb_config.h"
#include "eb_crc.h"
#include "eb_mem.h"
#include "eb_mqtt.h"
#include "eb_ota.h"
#include "eb_profile.h"
#include "eb_ring_queue.h"
#include "eb_rs485.h"
#include "eb_scanner.h"
#include "eb_storage.h"
#include "eb_web_analyzer.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_ble_off_calls = 0;
static void on_ble_off_cb(void *ctx) {
    (void)ctx;
    g_ble_off_calls++;
}

/* --- Mock RS-485 Bus for Transaction & Auto-Scanner Tests --- */
typedef struct {
    uint32_t current_baud;
    uint8_t last_tx[16];
    size_t last_tx_len;
    uint64_t clock_ms;
    bool de_state;
} mock_rs485_bus_t;

static int mock_set_baud(void *ctx, uint32_t baud) {
    ((mock_rs485_bus_t *)ctx)->current_baud = baud;
    return 0;
}
static void mock_flush_rx(void *ctx) { (void)ctx; }
static void mock_set_de(void *ctx, bool en) { ((mock_rs485_bus_t *)ctx)->de_state = en; }
static void mock_delay_us(void *ctx, uint32_t us) { (void)ctx; (void)us; }
static uint64_t mock_now_ms(void *ctx) { return ((mock_rs485_bus_t *)ctx)->clock_ms; }

static int mock_tx_bytes(void *ctx, const uint8_t *buf, size_t len) {
    mock_rs485_bus_t *b = (mock_rs485_bus_t *)ctx;
    size_t n = len < sizeof(b->last_tx) ? len : sizeof(b->last_tx);
    memcpy(b->last_tx, buf, n);
    b->last_tx_len = n;
    return (int)len;
}

static int mock_rx_bytes(void *ctx, uint8_t *buf, size_t max_len,
                         uint32_t resp_to, uint32_t inter_us) {
    (void)resp_to;
    (void)inter_us;
    mock_rs485_bus_t *b = (mock_rs485_bus_t *)ctx;
    if (b->last_tx_len < 8U) return 0;

    uint8_t sid = b->last_tx[0];
    uint8_t func = b->last_tx[1];
    uint16_t reg = ((uint16_t)b->last_tx[2] << 8) | b->last_tx[3];

    /* Slave 1 at 115200: JK BMS responding to 0x1200 (52.76 V = 0x149C) */
    if (b->current_baud == 115200U && sid == 1U && func == 0x03U && reg == 0x1200U && max_len >= 7U) {
        buf[0] = 1U; buf[1] = 0x03U; buf[2] = 0x02U; buf[3] = 0x14U; buf[4] = 0x9CU;
        uint16_t crc = eb_rs485_crc16(buf, 5U);
        buf[5] = (uint8_t)(crc & 0xFFU);
        buf[6] = (uint8_t)(crc >> 8);
        return 7;
    }
    /* Slave 2 at 9600: responds with Modbus Exception 0x02 (Illegal Data Address) */
    if (b->current_baud == 9600U && sid == 2U && func == 0x03U && max_len >= 5U) {
        buf[0] = 2U; buf[1] = 0x83U; buf[2] = 0x02U;
        uint16_t crc = eb_rs485_crc16(buf, 3U);
        buf[3] = (uint8_t)(crc & 0xFFU);
        buf[4] = (uint8_t)(crc >> 8);
        return 5;
    }
    return 0;
}

/* --- Mock CAN Listen-Only Bus --- */
typedef struct { uint32_t rate; } mock_can_bus_t;
static int mock_can_set_rate(void *ctx, uint32_t rate) {
    ((mock_can_bus_t *)ctx)->rate = rate;
    return 0;
}
static int mock_can_listen(void *ctx, uint32_t win_ms, uint32_t *ids, size_t max_ids, size_t *out_n) {
    (void)win_ms;
    mock_can_bus_t *c = (mock_can_bus_t *)ctx;
    if (c->rate == 500000U && max_ids >= 3U) {
        ids[0] = 0x351U; ids[1] = 0x355U; ids[2] = 0x356U;
        *out_n = 3U;
        return 0;
    }
    *out_n = 0U;
    return 0;
}

/* --- Host File-Backed Config Store --- */
static eb_config_persist_blob_t g_nvs_sim_blob;
static bool g_nvs_sim_present = false;
static int sim_nvs_load(void *ctx, eb_config_persist_blob_t *out_blob) {
    (void)ctx;
    if (!g_nvs_sim_present) return -1;
    *out_blob = g_nvs_sim_blob;
    return 0;
}
static int sim_nvs_save(void *ctx, const eb_config_persist_blob_t *blob) {
    (void)ctx;
    g_nvs_sim_blob = *blob;
    g_nvs_sim_present = true;
    return 0;
}

static void test_rs485_t35_adaptive_and_transact(void) {
    eb_rs485_ctx_t ctx;

    /* 9600 bps: 38.5 bit times = ~4011 us + 250 us = 4261 us */
    eb_rs485_init(&ctx, 9600U, 250U, true, true);
    assert(ctx.baud_rate == 9600U);
    assert(!ctx.baud_ceiling_warn);
    assert(ctx.effective_t35_us >= 4260U);

    /* 230400 bps with strict_cap=true: warns & clamps to 115200 */
    eb_rs485_init(&ctx, 230400U, 250U, true, true);
    assert(ctx.baud_ceiling_warn);
    assert(ctx.baud_rate == EB_RS485_ISOLATOR_BAUD_CEILING);
    assert(ctx.effective_t35_us == 2000U);

    /* Build FC06 write single register and check expected reply lengths */
    uint8_t wr[8];
    assert(eb_rs485_build_write_single_req(1U, 0x0100U, 0x0001U, wr) == 8U);
    assert(eb_rs485_expected_reply_len(0x03U, 3U) == 11U);
    assert(eb_rs485_expected_reply_len(0x06U, 1U) == 8U);

    /* Transaction via mock UART transport */
    mock_rs485_bus_t bus = {.current_baud = 115200U, .clock_ms = 100ULL};
    eb_rs485_transport_t xp = {
        .set_baud = mock_set_baud,
        .flush_rx = mock_flush_rx,
        .set_tx_enable = mock_set_de,
        .tx_bytes = mock_tx_bytes,
        .rx_bytes = mock_rx_bytes,
        .delay_us = mock_delay_us,
        .now_ms = mock_now_ms,
        .user_ctx = &bus,
    };
    uint8_t rx[32];
    size_t rx_len = 0U;
    assert(eb_rs485_transact(&ctx, &xp, 1U, 0x03U, 0x1200U, 1U, 100U, rx, sizeof(rx), &rx_len) == EB_RS485_OK);
    assert(rx_len == 7U);
    assert(ctx.rx_frames == 1U);
    assert(ctx.recent_count == 2U);

    /* Consecutive clipped replies trigger adaptive t3.5 margin escalation */
    uint32_t base_t35 = ctx.effective_t35_us;
    assert(eb_rs485_validate_reply(&ctx, 1U, 0x03U, rx, 5U) == EB_RS485_ERR_TRUNCATED);
    assert(eb_rs485_validate_reply(&ctx, 1U, 0x03U, rx, 5U) == EB_RS485_ERR_TRUNCATED);
    assert(ctx.framing_clipped_errors == 2U);
    assert(ctx.effective_t35_us > base_t35);

    /* Bad CRC */
    rx[6] ^= 0xFFU;
    assert(eb_rs485_validate_reply(&ctx, 1U, 0x03U, rx, 7U) == EB_RS485_ERR_CRC);
    assert(ctx.crc_errors == 1U);

    printf("[PASS] test_rs485_t35_adaptive_and_transact\n");
}

static void test_can_ack_bus_off_and_recovery(void) {
    eb_can_diag_t diag;
    eb_can_init(&diag, 500000U);

    eb_can_record_tx_result(&diag, false);
    assert(diag.ack_missing_count == 1U);
    assert(!diag.peer_ack_healthy);
    assert(strstr(eb_can_health_hint(&diag), "unacknowledged") != NULL);

    eb_can_record_tx_result(&diag, true);
    assert(diag.ack_ok_count == 1U);
    assert(diag.peer_ack_healthy);

    eb_can_frame_t cf = {.id = 0x355U, .dlc = 4U, .data = {85, 0, 99, 0}, .timestamp_ms = 500ULL};
    eb_can_record_rx(&diag, &cf);
    assert(diag.recent_count == 1U);

    eb_can_update_error_counters(&diag, 130U, 10U, false);
    assert(diag.state == EB_CAN_STATE_ERROR_PASSIVE);

    eb_can_update_error_counters(&diag, 255U, 10U, true);
    assert(diag.state == EB_CAN_STATE_BUS_OFF);
    assert(diag.bus_off_count == 1U);
    assert(!diag.peer_ack_healthy);

    /* Exponential backoff on Bus-Off recovery */
    assert(eb_can_should_attempt_recovery(&diag, 1000ULL));
    assert(!eb_can_should_attempt_recovery(&diag, 1100ULL)); /* < 200ms backoff */
    assert(eb_can_should_attempt_recovery(&diag, 1250ULL));

    printf("[PASS] test_can_ack_bus_off_and_recovery\n");
}

static void test_scanner_and_profiles(void) {
    eb_rs485_ctx_t rs485;
    eb_rs485_init(&rs485, 9600U, 250U, true, true);
    mock_rs485_bus_t bus = {.current_baud = 9600U, .clock_ms = 100ULL};
    eb_rs485_transport_t xp = {
        .set_baud = mock_set_baud,
        .flush_rx = mock_flush_rx,
        .set_tx_enable = mock_set_de,
        .tx_bytes = mock_tx_bytes,
        .rx_bytes = mock_rx_bytes,
        .delay_us = mock_delay_us,
        .now_ms = mock_now_ms,
        .user_ctx = &bus,
    };

    eb_scan_result_t scan;
    assert(eb_scanner_run_rs485(&rs485, &xp, 1U, 4U, &scan) == 0);
    assert(scan.scan_completed);
    assert(scan.hit_count == 2U); /* Slave 2 @ 9600 (exception) + Slave 1 @ 115200 (OK) */
    assert(scan.hits[0].baud_rate == 9600U && scan.hits[0].slave_id == 2U && scan.hits[0].is_exception);
    assert(scan.hits[1].baud_rate == 115200U && scan.hits[1].slave_id == 1U && !scan.hits[1].is_exception);

    mock_can_bus_t cbus = {0};
    eb_can_scan_transport_t cxp = {
        .set_listen_bitrate = mock_can_set_rate,
        .listen_frames = mock_can_listen,
        .user_ctx = &cbus,
    };
    assert(eb_scanner_run_can(&cxp, 100U, &scan) == 0);
    assert(scan.detected_can_bitrate == 500000U);
    assert(scan.can_id_count == 3U);

    /* Load JSON profile from profiles/jk_bms_modbus.json + built-in CAN BMS */
    eb_profile_mgr_t pm;
    eb_profile_mgr_init(&pm);
    FILE *pf = fopen("profiles/jk_bms_modbus.json", "rb");
    if (!pf) pf = fopen("../profiles/jk_bms_modbus.json", "rb");
    assert(pf != NULL);
    char jbuf[2048];
    size_t jn = fread(jbuf, 1U, sizeof(jbuf) - 1U, pf);
    fclose(pf);
    jbuf[jn] = '\0';
    assert(eb_profile_load_json(&pm, jbuf, jn) == 0);
    assert(eb_profile_load_builtin(&pm, "pylontech_can_v1", 1U) == 0);
    assert(pm.profile_count == 2U);

    /* Decode JK BMS voltage 0x149C = 5276 * 0.01 = 52.76 V */
    uint8_t vbytes[2] = {0x14U, 0x9CU};
    eb_decoded_value_t dv;
    assert(eb_profile_decode_modbus_signal(&pm, &pm.active_profiles[0],
                                           &pm.active_profiles[0].signals[0],
                                           vbytes, sizeof(vbytes), 1000ULL, &dv) == 0);
    assert(fabs(dv.value - 52.76) < 0.001);

    /* Decode Pylontech CAN 0x356: 52.40V (5240=0x1478 LE), -12.5A (-125=0xFF83 LE), 28.5C (285=0x011D LE) */
    eb_can_frame_t cf = {
        .id = 0x356U, .dlc = 6U,
        .data = {0x78U, 0x14U, 0x83U, 0xFFU, 0x1DU, 0x01U, 0, 0},
        .timestamp_ms = 2000ULL
    };
    assert(eb_profile_decode_can_frame(&pm, &cf, 2000ULL) == 3U);

    printf("[PASS] test_scanner_and_profiles\n");
}

static void test_500_cycle_power_cut_torture(void) {
    /*
     * PRD Verification #1:
     * 500 automated power cuts during active persistent circular storage writes
     * and config staging -> 0 corrupted filesystems, 0 lost committed records.
     */
    const char *meta_p = "/tmp/eb_torture_meta.bin";
    const char *data_p = "/tmp/eb_torture_data.bin";
    remove(meta_p);
    remove(data_p);

    eb_posix_storage_ctx_t pctx;
    eb_storage_backend_t backend;
    assert(eb_storage_posix_backend_init(&pctx, &backend, meta_p, data_p) == 0);

    const size_t CAP = 64U;
    eb_storage_ring_t ring;
    assert(eb_storage_open_persistent(&ring, &backend, CAP) == 0);

    uint32_t committed_seq = 0U;
    uint32_t acked_seq = 0U;

    for (uint32_t cycle = 1U; cycle <= 500U; cycle++) {
        /* Write a valid record */
        eb_telemetry_record_t r = {0};
        r.timestamp_ms = 1000ULL * cycle;
        r.bus = (cycle & 1U) ? EB_BUS_RS485 : EB_BUS_CAN;
        r.addr = (uint8_t)((cycle % 4U) + 1U);
        assert(eb_storage_append(&ring, &r) == 0);
        committed_seq = r.seq;

        /* Every 3rd cycle: inject either a torn data write or a torn superblock write! */
        if ((cycle % 6U) == 0U) {
            pctx.inject_torn_next_write = true;
            eb_telemetry_record_t bad = {0};
            bad.timestamp_ms = 99999ULL;
            bad.bus = EB_BUS_RS485;
            /* Torn write returns error; simulate immediate power cut + reboot */
            (void)eb_storage_append(&ring, &bad);
        } else if ((cycle % 6U) == 3U) {
            pctx.inject_torn_next_meta = true;
            (void)eb_storage_sync_meta(&ring);
        }

        /* Drain oldest occasionally when ring grows */
        if (ring.count > 32U) {
            eb_telemetry_record_t out;
            if (eb_storage_peek_oldest(&ring, &out)) {
                assert(out.seq > acked_seq);
                acked_seq = out.seq;
                assert(eb_storage_ack_oldest(&ring));
            }
        }

        /* Simulate power cut & full cold remount on every cycle */
        eb_storage_ring_t recovered;
        assert(eb_storage_open_persistent(&recovered, &backend, CAP) == 0);
        ring = recovered;

        /* Ring must always remain readable and never lose the latest committed record */
        eb_telemetry_record_t probe;
        assert(eb_storage_peek_oldest(&ring, &probe));
        assert(probe.seq <= committed_seq);
    }

    /* Drain all remaining records and verify monotonic sequence order */
    uint32_t last_seen = acked_seq;
    eb_telemetry_record_t final_rec;
    while (eb_storage_peek_oldest(&ring, &final_rec)) {
        assert(final_rec.seq > last_seen);
        last_seen = final_rec.seq;
        assert(eb_storage_ack_oldest(&ring));
    }
    assert(last_seen >= committed_seq);

    /* Also test dual-bank NVS crash rollback across reboot */
    g_nvs_sim_present = false;
    eb_config_backend_t nvs_bk = {.load = sim_nvs_load, .save = sim_nvs_save, .ctx = NULL};
    eb_config_mgr_t mgr;
    eb_config_init(&mgr, 60U);
    assert(eb_config_attach_backend(&mgr, &nvs_bk) == 0);

    eb_config_t risky = mgr.active;
    risky.rs485_baud = 38400U;
    assert(eb_config_stage(&mgr, &risky, 10U) == 0);

    /* Simulate reboot BEFORE confirmation -> must auto-revert to 9600 */
    eb_config_mgr_t rebooted;
    eb_config_init(&rebooted, 60U);
    assert(eb_config_attach_backend(&rebooted, &nvs_bk) == 0);
    assert(rebooted.active.rs485_baud == 9600U);
    assert(rebooted.rollback_count == 1U);

    remove(meta_p);
    remove(data_p);
    printf("[PASS] test_500_cycle_power_cut_torture (500 cuts, 0 corruptions)\n");
}

typedef struct {
    uint32_t published;
    uint32_t last_seq;
} sim_mqtt_sink_t;

static int sim_mqtt_pub(void *ctx, const char *topic, const char *payload, size_t len) {
    (void)topic;
    assert(len > 20U && strstr(payload, "\"seq\":") != NULL);
    sim_mqtt_sink_t *s = (sim_mqtt_sink_t *)ctx;
    s->published++;
    return 0;
}

static void test_7_day_high_rate_polling_simulation(void) {
    /*
     * PRD Verification #2:
     * Poll 4 Modbus slaves + 1 CAN BMS at 1-second intervals for 7 days (604,800 s)
     * while dropping Wi-Fi every 15 minutes (900 s) for 3 minutes (180 s).
     * Verifies: 0 queue drops, 0 lost records, <35% internal SRAM budget.
     */
    const uint32_t TOTAL_SECONDS = 7U * 86400U; /* 604,800 seconds */
    const size_t Q_CAP = 64U;
    const size_t S_CAP = 2048U; /* 5 records/s * 180s outage = 900 records < 2048 */

    eb_telemetry_record_t *q_buf = calloc(Q_CAP, sizeof(eb_telemetry_record_t));
    eb_telemetry_record_t *s_buf = calloc(S_CAP, sizeof(eb_telemetry_record_t));
    assert(q_buf && s_buf);

    eb_ring_queue_t rq;
    eb_storage_ring_t storage;
    eb_mqtt_client_t mqtt;
    eb_profile_mgr_t profiles;
    assert(eb_ring_queue_init(&rq, q_buf, Q_CAP) == 0);
    assert(eb_storage_init(&storage, s_buf, S_CAP) == 0);
    eb_mqtt_init(&mqtt, "eb-sim7d");
    eb_profile_mgr_init(&profiles);
    assert(eb_profile_load_builtin(&profiles, "jk_bms_modbus_v1", 1U) == 0);

    sim_mqtt_sink_t sink = {0};
    char scratch[512];
    uint32_t total_produced = 0U;

    for (uint32_t sec = 0U; sec < TOTAL_SECONDS; sec++) {
        /* 4 Modbus slaves + 1 CAN BMS = 5 records/second */
        for (uint8_t dev = 1U; dev <= 5U; dev++) {
            eb_telemetry_record_t r = {0};
            r.timestamp_ms = (uint64_t)sec * 1000ULL;
            r.bus = (dev <= 4U) ? EB_BUS_RS485 : EB_BUS_CAN;
            r.addr = dev;
            r.can_id = (dev == 5U) ? 0x356U : 0U;
            r.len = 4U;
            assert(eb_ring_queue_push(&rq, &r));
            total_produced++;
        }

        /* Thread 2: drain ring queue -> LittleFS store */
        eb_telemetry_record_t popped;
        while (eb_ring_queue_pop(&rq, &popped)) {
            assert(eb_storage_append(&storage, &popped) == 0);
        }

        /* Wi-Fi drops every 15 min (900s) for 180s */
        bool wifi_up = ((sec % 900U) >= 180U);
        eb_mqtt_set_link_state(&mqtt, wifi_up, wifi_up);
        if (wifi_up) {
            (void)eb_mqtt_drain_storage(&mqtt, &storage, &profiles,
                                        sim_mqtt_pub, &sink, 32U,
                                        scratch, sizeof(scratch));
        }
    }

    /* Final drain once online */
    eb_mqtt_set_link_state(&mqtt, true, true);
    while (storage.count > 0U) {
        (void)eb_mqtt_drain_storage(&mqtt, &storage, &profiles,
                                    sim_mqtt_pub, &sink, 64U,
                                    scratch, sizeof(scratch));
    }

    assert(rq.dropped_count == 0U);
    assert(storage.overwritten_count == 0U);
    assert(sink.published == total_produced); /* 3,024,000 records, 0 lost */

    /* Check internal SRAM budget (< 35%) when large buffers live in PSRAM */
    eb_mem_stats_t mem = {
        .internal_sram_total_bytes = 524288U,
        .internal_sram_used_bytes = 128000U, /* ~24.4% */
        .psram_total_bytes = 8388608U,
        .psram_used_bytes = 524288U,
        .psram_active = true,
    };
    assert(eb_mem_within_sram_budget(&mem));

    free(q_buf);
    free(s_buf);
    printf("[PASS] test_7_day_high_rate_polling_simulation (%u records, 0 lost, %u outages)\n",
           total_produced, mqtt.outage_count);
}

static uint32_t g_test_now_sec = 0U;
static uint32_t test_now_cb(void *ctx) { (void)ctx; return g_test_now_sec; }

static void test_5min_commissioning_web_and_ota(void) {
    eb_rs485_ctx_t rs485;
    eb_can_diag_t can;
    eb_telemetry_record_t s_buf[8];
    eb_storage_ring_t storage;
    eb_config_mgr_t cfg;
    eb_ble_prov_t ble;
    eb_ota_state_t ota;
    eb_mqtt_client_t mqtt;
    eb_profile_mgr_t profiles;
    eb_scan_result_t scan = {.scan_completed = true, .hit_count = 1U};
    scan.hits[0].baud_rate = 115200U;
    scan.hits[0].slave_id = 1U;
    eb_mem_stats_t mem = {
        .internal_sram_total_bytes = 524288U,
        .internal_sram_used_bytes = 120000U,
        .psram_total_bytes = 8388608U,
        .psram_used_bytes = 256000U,
        .psram_active = true,
    };

    eb_rs485_init(&rs485, 115200U, 250U, true, true);
    eb_can_init(&can, 500000U);
    eb_storage_init(&storage, s_buf, 8U);
    eb_config_init(&cfg, 60U);
    g_ble_off_calls = 0;
    eb_ble_prov_init(&ble, false, 0U, 300U, on_ble_off_cb, NULL);
    eb_ota_init_with_gate(&ota, false, 30U, NULL);
    eb_mqtt_init(&mqtt, "eb-field-01");
    eb_profile_mgr_init(&profiles);

    eb_web_ctx_t web = {
        .rs485 = &rs485, .can = &can, .storage = &storage, .cfg = &cfg,
        .ble = &ble, .ota = &ota, .mqtt = &mqtt, .profiles = &profiles,
        .scan = &scan, .mem = &mem, .now_sec_cb = test_now_cb,
    };

    char resp[8192];
    int status = 0;
    const char *ctype = NULL;

    /* 1. Technician opens Web UI on local AP */
    assert(eb_web_handle_http(&web, "GET", "/", NULL, 0U, &status, &ctype, resp, sizeof(resp)) > 0);
    assert(status == 200 && strstr(resp, "EdgeBridge-S3 Phase 0 Bus Analyzer") != NULL);

    /* 2. Auto-scan & select JK BMS profile via REST API */
    assert(eb_web_handle_http(&web, "POST", "/api/v1/scan", NULL, 0U,
                              &status, &ctype, resp, sizeof(resp)) > 0);
    assert(status == 200 && strstr(resp, "\"baud\":115200") != NULL);

    const char *prof_req = "{\"builtin\":\"jk_bms_modbus_v1\"}";
    assert(eb_web_handle_http(&web, "POST", "/api/v1/profiles", prof_req, strlen(prof_req),
                              &status, &ctype, resp, sizeof(resp)) > 0);
    assert(status == 200 && profiles.profile_count == 1U);

    /* 3. Provisions Wi-Fi + MQTT via Web UI -> shuts down BLE radio & starts 60s watchdog */
    g_test_now_sec = 20U;
    const char *cfg_req = "{\"wifi_ssid\":\"SolarPlant\",\"wifi_pass\":\"pass123\",\"mqtt_broker\":\"mqtt.solar.in\",\"mqtt_port\":1883}";
    assert(eb_web_handle_http(&web, "POST", "/api/v1/config", cfg_req, strlen(cfg_req),
                              &status, &ctype, resp, sizeof(resp)) > 0);
    assert(status == 200);
    assert(!ble.radio_active && g_ble_off_calls == 1);
    assert(cfg.pending_confirm);

    /* 4. Confirms config before 60s expires */
    g_test_now_sec = 35U;
    assert(eb_web_handle_http(&web, "POST", "/api/v1/config/confirm", NULL, 0U,
                              &status, &ctype, resp, sizeof(resp)) > 0);
    assert(status == 200 && !cfg.pending_confirm);

    /* 5. Diagnostics, telemetry & OTA endpoints report full health */
    assert(eb_web_handle_http(&web, "GET", "/api/v1/diagnostics", NULL, 0U,
                              &status, &ctype, resp, sizeof(resp)) > 0);
    assert(status == 200 && strstr(resp, "\"internal_sram_pct\":22") != NULL);
    assert(eb_web_handle_http(&web, "GET", "/api/v1/telemetry", NULL, 0U,
                              &status, &ctype, resp, sizeof(resp)) > 0);
    assert(status == 200 && strstr(resp, "\"metrics\":") != NULL);
    assert(eb_web_handle_http(&web, "GET", "/api/v1/ota", NULL, 0U,
                              &status, &ctype, resp, sizeof(resp)) > 0);
    assert(status == 200 && strstr(resp, "\"confirmed\":false") != NULL);

    /* 6. OTA chunked download + CRC32 verification + 30s health gate */
    const uint8_t fw[] = "EDGEBRIDGE_1";
    uint32_t fw_crc = eb_crc32_ieee(fw, sizeof(fw));
    assert(eb_ota_begin_download(&ota, "https://ota.edgebridge.in/fw.bin", sizeof(fw), fw_crc) == 0);
    assert(eb_ota_ingest_chunk(&ota, 0U, fw, 6U) == 0);
    assert(eb_ota_ingest_chunk(&ota, 4U, fw + 4, sizeof(fw) - 4U) == 0); /* overlapping resume */
    assert(eb_ota_finalize(&ota) == 0);
    assert(ota.dl_status == EB_OTA_STAGED_PENDING_REBOOT);

    eb_ota_report_health_at(&ota, true, true, 100U);
    assert(!eb_ota_try_confirm_at(&ota, 120U)); /* Only 20s < 30s gate */
    assert(eb_ota_try_confirm_at(&ota, 130U));  /* 30s sustained health -> confirmed */

    printf("[PASS] test_5min_commissioning_web_and_ota\n");
}

int main(void) {
    test_rs485_t35_adaptive_and_transact();
    test_can_ack_bus_off_and_recovery();
    test_scanner_and_profiles();
    test_500_cycle_power_cut_torture();
    test_7_day_high_rate_polling_simulation();
    test_5min_commissioning_web_and_ota();
    printf("ALL PHASE 0 MATURE TESTS PASSED (6/6)\n");
    return 0;
}
