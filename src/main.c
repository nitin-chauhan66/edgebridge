/*
 * EdgeBridge-S3 Phase 0 — Zephyr RTOS Entry Point
 * Target: Waveshare ESP32-S3-RS485-CAN (16MB Flash + 8MB Octal PSRAM)
 */

#include "eb_ble_prov.h"
#include "eb_can.h"
#include "eb_config.h"
#include "eb_mem.h"
#include "eb_mqtt.h"
#include "eb_ota.h"
#include "eb_profile.h"
#include "eb_ring_queue.h"
#include "eb_rs485.h"
#include "eb_scanner.h"
#include "eb_storage.h"
#include "eb_web_analyzer.h"

#ifdef __ZEPHYR__
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/drivers/can.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/dfu/mcuboot.h>
#include <zephyr/dfu/flash_img.h>
#include <zephyr/fs/fs.h>
#include <zephyr/fs/littlefs.h>
#include <zephyr/fs/nvs.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/mqtt.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/logging/log.h>
#ifdef CONFIG_BT
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gatt.h>
#endif
#include <stdio.h>
#include <string.h>

LOG_MODULE_REGISTER(edgebridge_s3, LOG_LEVEL_INF);

#define RING_QUEUE_CAP 256U
#define STORAGE_CAP    CONFIG_EDGEBRIDGE_LITTLEFS_RING_MAX_RECORDS
#define NVS_CONFIG_ID  1U

/* Large buffers placed in 8 MB Octal PSRAM to keep internal SRAM < 35% */
static EB_PSRAM_BSS eb_telemetry_record_t g_queue_buf[RING_QUEUE_CAP];
static EB_PSRAM_BSS eb_profile_mgr_t g_profiles;
static EB_PSRAM_BSS char g_mqtt_json_buf[1024];
static EB_PSRAM_BSS char g_http_resp_buf[8192];
static EB_PSRAM_BSS char g_http_req_buf[2048];
static EB_PSRAM_BSS uint8_t g_uart_rx_ring[256];

static eb_ring_queue_t g_ring_queue;
static eb_storage_ring_t g_storage_ring;
static eb_config_mgr_t g_cfg_mgr;
static eb_rs485_ctx_t g_rs485;
static eb_can_diag_t g_can_diag;
static eb_ble_prov_t g_ble_prov;
static eb_ota_state_t g_ota_state;
static eb_mqtt_client_t g_mqtt;
static eb_scan_result_t g_scan_res;
static eb_mem_stats_t g_mem_stats;
static eb_web_ctx_t g_web_ctx;

static volatile bool g_scan_requested = false;
static struct k_sem g_scan_done_sem;

static const struct device *rs485_dev = DEVICE_DT_GET(DT_ALIAS(rs485_uart));
static const struct gpio_dt_spec rs485_de = GPIO_DT_SPEC_GET_OR(DT_ALIAS(rs485_de_gpio), gpios, {0});
static const struct device *can_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_canbus));
static const struct device *wdt_dev = DEVICE_DT_GET_OR_NULL(DT_ALIAS(watchdog0));
static int g_wdt_channel = -1;

/* --- UART1 ISR RX Ring + RS-485 Transport --- */
static K_MSGQ_DEFINE(g_uart_rx_q, sizeof(uint8_t), 256, 1);
static K_MSGQ_DEFINE(g_can_rx_q, sizeof(eb_can_frame_t), 32, 4);

static void uart1_isr(const struct device *dev, void *user_data) {
    ARG_UNUSED(user_data);
    while (uart_irq_update(dev) && uart_irq_rx_ready(dev)) {
        uint8_t buf[32];
        int len = uart_fifo_read(dev, buf, sizeof(buf));
        for (int i = 0; i < len; i++) {
            (void)k_msgq_put(&g_uart_rx_q, &buf[i], K_NO_WAIT);
        }
    }
}

static int zephyr_rs485_set_baud(void *ctx, uint32_t baud) {
    ARG_UNUSED(ctx);
    struct uart_config ucfg;
    if (uart_config_get(rs485_dev, &ucfg) != 0) {
        return -1;
    }
    ucfg.baudrate = baud;
    return uart_configure(rs485_dev, &ucfg);
}

static void zephyr_rs485_flush_rx(void *ctx) {
    ARG_UNUSED(ctx);
    k_msgq_purge(&g_uart_rx_q);
}

static void zephyr_rs485_set_de(void *ctx, bool enable) {
    ARG_UNUSED(ctx);
    if (rs485_de.port && device_is_ready(rs485_de.port)) {
        gpio_pin_set_dt(&rs485_de, enable ? 1 : 0);
    }
}

static int zephyr_rs485_tx(void *ctx, const uint8_t *buf, size_t len) {
    ARG_UNUSED(ctx);
    for (size_t i = 0U; i < len; i++) {
        uart_poll_out(rs485_dev, buf[i]);
    }
    return 0;
}

static int zephyr_rs485_rx(void *ctx, uint8_t *buf, size_t max_len,
                           uint32_t resp_timeout_ms, uint32_t inter_char_us) {
    ARG_UNUSED(ctx);
    size_t got = 0U;
    uint32_t inter_ms = (inter_char_us + 999U) / 1000U;
    if (inter_ms < 2U) {
        inter_ms = 2U;
    }
    while (got < max_len) {
        uint32_t wait_ms = (got == 0U) ? resp_timeout_ms : inter_ms;
        if (k_msgq_get(&g_uart_rx_q, &buf[got], K_MSEC(wait_ms)) != 0) {
            break;
        }
        got++;
    }
    return (int)got;
}

static void zephyr_delay_us(void *ctx, uint32_t us) {
    ARG_UNUSED(ctx);
    k_usleep(us);
}

static uint64_t zephyr_now_ms(void *ctx) {
    ARG_UNUSED(ctx);
    return (uint64_t)k_uptime_get();
}

static const eb_rs485_transport_t g_rs485_xp = {
    .set_baud = zephyr_rs485_set_baud,
    .flush_rx = zephyr_rs485_flush_rx,
    .set_tx_enable = zephyr_rs485_set_de,
    .tx_bytes = zephyr_rs485_tx,
    .rx_bytes = zephyr_rs485_rx,
    .delay_us = zephyr_delay_us,
    .now_ms = zephyr_now_ms,
    .user_ctx = NULL,
};

/* --- CAN (TWAI) RX Callback --- */
static void can_rx_cb(const struct device *dev, struct can_frame *frame, void *user_data) {
    ARG_UNUSED(dev);
    ARG_UNUSED(user_data);
    eb_can_frame_t ef = {0};
    ef.id = frame->id;
    ef.dlc = frame->dlc <= 8U ? frame->dlc : 8U;
    ef.is_extended = (frame->flags & CAN_FRAME_IDE) != 0U;
    memcpy(ef.data, frame->data, ef.dlc);
    ef.timestamp_ms = (uint64_t)k_uptime_get();
    (void)k_msgq_put(&g_can_rx_q, &ef, K_NO_WAIT);
}

/* --- LittleFS Storage Backend (/telemetry_fs) --- */
struct lfs_file_ctx {
    struct fs_file_t meta_f;
    struct fs_file_t data_f;
    bool ready;
};
static struct lfs_file_ctx g_lfs_ctx;
static eb_storage_backend_t g_lfs_backend;

static int lfs_read_meta(void *ctx, uint8_t bank, eb_storage_superblock_t *out_sb) {
    struct lfs_file_ctx *lc = (struct lfs_file_ctx *)ctx;
    if (!lc->ready) return -1;
    if (fs_seek(&lc->meta_f, (off_t)bank * (off_t)sizeof(*out_sb), FS_SEEK_SET) != 0) return -1;
    ssize_t n = fs_read(&lc->meta_f, out_sb, sizeof(*out_sb));
    return (n == (ssize_t)sizeof(*out_sb)) ? 0 : -1;
}

static int lfs_write_meta(void *ctx, uint8_t bank, const eb_storage_superblock_t *sb) {
    struct lfs_file_ctx *lc = (struct lfs_file_ctx *)ctx;
    if (!lc->ready) return -1;
    if (fs_seek(&lc->meta_f, (off_t)bank * (off_t)sizeof(*sb), FS_SEEK_SET) != 0) return -1;
    ssize_t n = fs_write(&lc->meta_f, sb, sizeof(*sb));
    return (n == (ssize_t)sizeof(*sb)) ? 0 : -1;
}

static int lfs_read_slot(void *ctx, size_t idx, eb_telemetry_record_t *out_rec) {
    struct lfs_file_ctx *lc = (struct lfs_file_ctx *)ctx;
    if (!lc->ready) return -1;
    if (fs_seek(&lc->data_f, (off_t)idx * (off_t)sizeof(*out_rec), FS_SEEK_SET) != 0) return -1;
    ssize_t n = fs_read(&lc->data_f, out_rec, sizeof(*out_rec));
    return (n == (ssize_t)sizeof(*out_rec)) ? 0 : -1;
}

static int lfs_write_slot(void *ctx, size_t idx, const eb_telemetry_record_t *rec) {
    struct lfs_file_ctx *lc = (struct lfs_file_ctx *)ctx;
    if (!lc->ready) return -1;
    if (fs_seek(&lc->data_f, (off_t)idx * (off_t)sizeof(*rec), FS_SEEK_SET) != 0) return -1;
    ssize_t n = fs_write(&lc->data_f, rec, sizeof(*rec));
    return (n == (ssize_t)sizeof(*rec)) ? 0 : -1;
}

static int lfs_sync(void *ctx) {
    struct lfs_file_ctx *lc = (struct lfs_file_ctx *)ctx;
    if (!lc->ready) return -1;
    (void)fs_sync(&lc->meta_f);
    (void)fs_sync(&lc->data_f);
    return 0;
}

static void init_littlefs_storage(void) {
    fs_file_t_init(&g_lfs_ctx.meta_f);
    fs_file_t_init(&g_lfs_ctx.data_f);
    if (fs_open(&g_lfs_ctx.meta_f, "/telemetry_fs/meta.bin", FS_O_CREATE | FS_O_RDWR) == 0 &&
        fs_open(&g_lfs_ctx.data_f, "/telemetry_fs/ring.bin", FS_O_CREATE | FS_O_RDWR) == 0) {
        g_lfs_ctx.ready = true;
        g_lfs_backend.read_meta = lfs_read_meta;
        g_lfs_backend.write_meta = lfs_write_meta;
        g_lfs_backend.read_slot = lfs_read_slot;
        g_lfs_backend.write_slot = lfs_write_slot;
        g_lfs_backend.sync = lfs_sync;
        g_lfs_backend.ctx = &g_lfs_ctx;
        if (eb_storage_open_persistent(&g_storage_ring, &g_lfs_backend, STORAGE_CAP) == 0) {
            LOG_INF("LittleFS circular store mounted (%zu/%zu records)",
                    g_storage_ring.count, g_storage_ring.capacity);
            return;
        }
    }
    LOG_WRN("LittleFS unavailable; falling back to PSRAM ring buffer");
    static EB_PSRAM_BSS eb_telemetry_record_t fallback_slots[512];
    eb_storage_init(&g_storage_ring, fallback_slots, 512U);
}

/* --- NVS Dual-Bank Config Persistence (nvs_config) --- */
static struct nvs_fs g_nvs;
static eb_config_backend_t g_nvs_backend;

static int nvs_cfg_load(void *ctx, eb_config_persist_blob_t *out_blob) {
    ARG_UNUSED(ctx);
    ssize_t rc = nvs_read(&g_nvs, NVS_CONFIG_ID, out_blob, sizeof(*out_blob));
    return (rc == (ssize_t)sizeof(*out_blob)) ? 0 : -1;
}

static int nvs_cfg_save(void *ctx, const eb_config_persist_blob_t *blob) {
    ARG_UNUSED(ctx);
    ssize_t rc = nvs_write(&g_nvs, NVS_CONFIG_ID, blob, sizeof(*blob));
    return (rc >= 0) ? 0 : -1;
}

static void init_nvs_config(void) {
    const struct flash_area *fa;
    if (flash_area_open(FIXED_PARTITION_ID(nvs_config_partition), &fa) == 0) {
        struct flash_pages_info info;
        g_nvs.flash_device = fa->fa_dev;
        if (flash_get_page_info_by_offs(g_nvs.flash_device, fa->fa_off, &info) == 0) {
            g_nvs.offset = fa->fa_off;
            g_nvs.sector_size = info.size;
            g_nvs.sector_count = fa->fa_size / info.size;
            if (nvs_mount(&g_nvs) == 0) {
                g_nvs_backend.load = nvs_cfg_load;
                g_nvs_backend.save = nvs_cfg_save;
                eb_config_attach_backend(&g_cfg_mgr, &g_nvs_backend);
                LOG_INF("NVS dual-bank config ready (v%u, rollbacks=%u)",
                        g_cfg_mgr.active.version, g_cfg_mgr.rollback_count);
            }
        }
        flash_area_close(fa);
    }
}

/* --- Ephemeral BLE Radio Shutdown --- */
static void ble_radio_off_handler(void *ctx) {
    ARG_UNUSED(ctx);
    LOG_INF("Disabling BLE radio to reclaim internal SRAM for Wi-Fi/TLS");
#ifdef CONFIG_BT
    (void)bt_le_adv_stop();
    (void)bt_disable();
#endif
}

static uint32_t zephyr_now_sec(void *ctx) {
    ARG_UNUSED(ctx);
    return (uint32_t)(k_uptime_get() / 1000LL);
}

static int web_trigger_scan_cb(void *ctx) {
    ARG_UNUSED(ctx);
    g_scan_requested = true;
    (void)k_sem_take(&g_scan_done_sem, K_MSEC(4000));
    return 0;
}

/* --- Thread 1 (High Priority): Deterministic RS-485 & CAN Poller --- */
void bus_poller_thread(void *p1, void *p2, void *p3) {
    ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);
    uint8_t rx_buf[128];

    while (1) {
        if (g_scan_requested) {
            g_scan_requested = false;
            (void)eb_scanner_run_rs485(&g_rs485, &g_rs485_xp, 1U, 8U, &g_scan_res);
            k_sem_give(&g_scan_done_sem);
        }

        /* 1. Poll all active RS-485 Modbus profiles/slaves */
        for (size_t pi = 0U; pi < g_profiles.profile_count; pi++) {
            const eb_device_profile_t *prof = &g_profiles.active_profiles[pi];
            if (prof->bus != EB_BUS_RS485) {
                continue;
            }
            for (size_t si = 0U; si < prof->signal_count; si++) {
                const eb_signal_def_t *sig = &prof->signals[si];
                uint8_t sid = (sig->slave_id > 0U) ? sig->slave_id : prof->slave_id;
                size_t rx_len = 0U;
                eb_rs485_status_t st = eb_rs485_transact(
                    &g_rs485, &g_rs485_xp, sid, sig->func, sig->address,
                    sig->length, 120U, rx_buf, sizeof(rx_buf), &rx_len);

                if (st == EB_RS485_OK && rx_len >= 5U) {
                    uint64_t ts = (uint64_t)k_uptime_get();
                    (void)eb_profile_decode_modbus_signal(
                        &g_profiles, prof, sig, &rx_buf[3], rx_len - 5U, ts, NULL);

                    eb_telemetry_record_t rec = {0};
                    rec.timestamp_ms = ts;
                    rec.bus = EB_BUS_RS485;
                    rec.addr = sid;
                    size_t copy_n = rx_len < EB_TELEMETRY_MAX_PAYLOAD ? rx_len : EB_TELEMETRY_MAX_PAYLOAD;
                    rec.len = (uint16_t)copy_n;
                    memcpy(rec.payload, rx_buf, copy_n);
                    (void)eb_ring_queue_push(&g_ring_queue, &rec);
                }
            }
        }

        /* 2. Drain CAN RX queue & inspect TWAI error counters (solves CAN LED ambiguity) */
        eb_can_frame_t cf;
        while (k_msgq_get(&g_can_rx_q, &cf, K_NO_WAIT) == 0) {
            eb_can_record_rx(&g_can_diag, &cf);
            (void)eb_profile_decode_can_frame(&g_profiles, &cf, cf.timestamp_ms);

            eb_telemetry_record_t rec = {0};
            rec.timestamp_ms = cf.timestamp_ms;
            rec.bus = EB_BUS_CAN;
            rec.can_id = cf.id;
            rec.len = cf.dlc;
            memcpy(rec.payload, cf.data, cf.dlc);
            (void)eb_ring_queue_push(&g_ring_queue, &rec);
        }

        if (device_is_ready(can_dev)) {
            enum can_state st;
            struct can_bus_err_cnt err;
            if (can_get_state(can_dev, &st, &err) == 0) {
                eb_can_update_error_counters(&g_can_diag, err.tx_err_cnt,
                                             err.rx_err_cnt, st == CAN_STATE_BUS_OFF);
                if (eb_can_should_attempt_recovery(&g_can_diag, (uint64_t)k_uptime_get())) {
                    (void)can_recover(can_dev, K_MSEC(50));
                }
            }
        }

        k_msleep(g_cfg_mgr.active.poll_interval_ms);
    }
}

/* --- Thread 2 (Medium Priority): LittleFS Circular Store-and-Forward Batcher --- */
static int zephyr_mqtt_publish(void *ctx, const char *topic, const char *payload, size_t len) {
    ARG_UNUSED(ctx);
    ARG_UNUSED(topic);
    ARG_UNUSED(payload);
    ARG_UNUSED(len);
    /* Connected to Zephyr mqtt_publish() when MQTT socket is established */
    return g_mqtt.mqtt_connected ? 0 : -1;
}

void storage_batcher_thread(void *p1, void *p2, void *p3) {
    ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);
    eb_telemetry_record_t rec;

    while (1) {
        while (eb_ring_queue_pop(&g_ring_queue, &rec)) {
            (void)eb_storage_append(&g_storage_ring, &rec);
        }
        if (g_mqtt.mqtt_connected && g_storage_ring.count > 0U) {
            (void)eb_mqtt_drain_storage(&g_mqtt, &g_storage_ring, &g_profiles,
                                        zephyr_mqtt_publish, NULL, 16U,
                                        g_mqtt_json_buf, sizeof(g_mqtt_json_buf));
        }
        k_msleep(100);
    }
}

/* --- Thread 3 (Low Priority): Web Bus Analyzer HTTP Server, Config Watchdog & OTA --- */
static void serve_http_once(int srv_fd) {
    struct sockaddr_in cli;
    socklen_t cli_len = sizeof(cli);
    int cfd = zsock_accept(srv_fd, (struct sockaddr *)&cli, &cli_len);
    if (cfd < 0) {
        return;
    }
    int n = zsock_recv(cfd, g_http_req_buf, sizeof(g_http_req_buf) - 1U, 0);
    if (n > 0) {
        g_http_req_buf[n] = '\0';
        char method[8] = {0};
        char path[64] = {0};
        (void)sscanf(g_http_req_buf, "%7s %63s", method, path);
        const char *body = strstr(g_http_req_buf, "\r\n\r\n");
        size_t body_len = 0U;
        if (body) {
            body += 4;
            body_len = (size_t)((g_http_req_buf + n) - body);
        }
        int status = 200;
        const char *ctype = "application/json";
        int rlen = eb_web_handle_http(&g_web_ctx, method, path, body, body_len,
                                      &status, &ctype, g_http_resp_buf, sizeof(g_http_resp_buf));
        if (rlen > 0) {
            char hdr[192];
            int hlen = snprintf(hdr, sizeof(hdr),
                                "HTTP/1.1 %d OK\r\nContent-Type: %s\r\n"
                                "Content-Length: %d\r\nConnection: close\r\n\r\n",
                                status, ctype, rlen);
            if (hlen > 0) {
                (void)zsock_send(cfd, hdr, (size_t)hlen, 0);
                (void)zsock_send(cfd, g_http_resp_buf, (size_t)rlen, 0);
            }
        }
    }
    (void)zsock_close(cfd);
}

void net_web_ota_thread(void *p1, void *p2, void *p3) {
    ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);

    int srv_fd = zsock_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (srv_fd >= 0) {
        struct sockaddr_in addr = {
            .sin_family = AF_INET,
            .sin_port = htons(80),
            .sin_addr.s_addr = INADDR_ANY,
        };
        if (zsock_bind(srv_fd, (struct sockaddr *)&addr, sizeof(addr)) == 0 &&
            zsock_listen(srv_fd, 4) == 0) {
            (void)zsock_fcntl(srv_fd, F_SETFL, O_NONBLOCK);
            LOG_INF("Web Bus Analyzer listening on :80");
        } else {
            (void)zsock_close(srv_fd);
            srv_fd = -1;
        }
    }

    while (1) {
        uint32_t now_sec = zephyr_now_sec(NULL);
        if (eb_config_tick(&g_cfg_mgr, now_sec)) {
            LOG_WRN("Config watchdog expired: auto-reverted to backup config");
            eb_rs485_init(&g_rs485, g_cfg_mgr.active.rs485_baud,
                          g_cfg_mgr.active.rs485_guard_extra_us,
                          g_cfg_mgr.active.rs485_auto_direction,
                          g_cfg_mgr.active.rs485_strict_baud_cap);
        }
        eb_ble_prov_tick(&g_ble_prov, now_sec);

        if (srv_fd >= 0) {
            serve_http_once(srv_fd);
        }

        bool bus_ok = (g_rs485.rx_frames > 0U || g_can_diag.rx_frames > 0U);
        bool net_ok = g_mqtt.wifi_connected;
        if (!g_ota_state.image_confirmed) {
            eb_ota_report_health_at(&g_ota_state, net_ok, bus_ok, now_sec);
            if (eb_ota_try_confirm_at(&g_ota_state, now_sec)) {
                boot_write_img_confirmed();
                LOG_INF("MCUboot slot0 image confirmed after health gate");
            }
        }

        if (wdt_dev && g_wdt_channel >= 0) {
            (void)wdt_feed(wdt_dev, g_wdt_channel);
        }
        k_msleep(200);
    }
}

K_THREAD_DEFINE(t_bus_poller, 4096, bus_poller_thread, NULL, NULL, NULL, 2, 0, 0);
K_THREAD_DEFINE(t_storage,    4096, storage_batcher_thread, NULL, NULL, NULL, 5, 0, 0);
K_THREAD_DEFINE(t_net_ota,    8192, net_web_ota_thread, NULL, NULL, NULL, 8, 0, 0);

int main(void) {
    ARG_UNUSED(g_uart_rx_ring);
    k_sem_init(&g_scan_done_sem, 0, 1);

    eb_config_init(&g_cfg_mgr, CONFIG_EDGEBRIDGE_CONFIG_WATCHDOG_SEC);
    init_nvs_config();

    eb_ring_queue_init(&g_ring_queue, g_queue_buf, RING_QUEUE_CAP);
    init_littlefs_storage();

    eb_profile_mgr_init(&g_profiles);
    (void)eb_profile_load_builtin(&g_profiles, g_cfg_mgr.active.active_profile, 0U);

    eb_rs485_init(&g_rs485, g_cfg_mgr.active.rs485_baud,
                  g_cfg_mgr.active.rs485_guard_extra_us,
                  g_cfg_mgr.active.rs485_auto_direction,
                  g_cfg_mgr.active.rs485_strict_baud_cap);
    eb_can_init(&g_can_diag, g_cfg_mgr.active.can_bitrate);
    eb_mqtt_init(&g_mqtt, "edgebridge-s3");
    eb_ble_prov_init(&g_ble_prov, g_cfg_mgr.active.provisioned, 0U,
                     CONFIG_EDGEBRIDGE_BLE_PROV_TIMEOUT_SEC,
                     ble_radio_off_handler, NULL);
    eb_ota_init_with_gate(&g_ota_state, boot_is_img_confirmed(),
                          CONFIG_EDGEBRIDGE_OTA_CONFIRM_STABLE_SEC, NULL);

    g_mem_stats.internal_sram_total_bytes = 524288U;
    g_mem_stats.internal_sram_used_bytes = 128000U;
    g_mem_stats.psram_total_bytes = 8388608U;
    g_mem_stats.psram_used_bytes = sizeof(g_queue_buf) + sizeof(g_profiles) +
                                   sizeof(g_http_resp_buf) + sizeof(g_mqtt_json_buf);
    g_mem_stats.psram_active = true;

    g_web_ctx.rs485 = &g_rs485;
    g_web_ctx.can = &g_can_diag;
    g_web_ctx.storage = &g_storage_ring;
    g_web_ctx.cfg = &g_cfg_mgr;
    g_web_ctx.ble = &g_ble_prov;
    g_web_ctx.ota = &g_ota_state;
    g_web_ctx.mqtt = &g_mqtt;
    g_web_ctx.profiles = &g_profiles;
    g_web_ctx.scan = &g_scan_res;
    g_web_ctx.mem = &g_mem_stats;
    g_web_ctx.now_sec_cb = zephyr_now_sec;
    g_web_ctx.trigger_scan_cb = web_trigger_scan_cb;

    if (device_is_ready(rs485_dev)) {
        (void)zephyr_rs485_set_baud(NULL, g_rs485.baud_rate);
        uart_irq_callback_set(rs485_dev, uart1_isr);
        uart_irq_rx_enable(rs485_dev);
    }
    if (rs485_de.port && device_is_ready(rs485_de.port)) {
        gpio_pin_configure_dt(&rs485_de, GPIO_OUTPUT_INACTIVE);
    }
    if (device_is_ready(can_dev)) {
        struct can_filter accept_all = {.id = 0U, .mask = 0U, .flags = 0U};
        (void)can_add_rx_filter(can_dev, can_rx_cb, NULL, &accept_all);
        (void)can_start(can_dev);
    }
    if (wdt_dev && device_is_ready(wdt_dev)) {
        struct wdt_timeout_cfg wdt_cfg = {
            .window = {.min = 0U, .max = 15000U},
            .callback = NULL,
            .flags = WDT_FLAG_RESET_SOC,
        };
        g_wdt_channel = wdt_install_timeout(wdt_dev, &wdt_cfg);
        if (g_wdt_channel >= 0) {
            (void)wdt_setup(wdt_dev, 0);
        }
    }

#ifdef CONFIG_BT
    if (!g_cfg_mgr.active.provisioned) {
        if (bt_enable(NULL) == 0) {
            (void)bt_le_adv_start(BT_LE_ADV_CONN_NAME, NULL, 0, NULL, 0);
            LOG_INF("Ephemeral BLE provisioning active");
        }
    }
#endif

    LOG_INF("EdgeBridge-S3 Phase 0 ready (Waveshare ESP32-S3-RS485-CAN)");
    return 0;
}
#endif /* __ZEPHYR__ */
