# Changelog

All notable changes to **EdgeBridge-S3** are documented in this file.

## [0.1.0-phase0] - 2026-11-24

### Added
- **Phase 0 Target Support (`Waveshare ESP32-S3-RS485-CAN`):** DeviceTree overlay (`boards/waveshare_esp32s3_rs485_can.overlay`), Kconfig options, and 16 MB MCUboot + LittleFS + NVS flash partition layout.
- **Hardened RS-485 Modbus RTU Engine (`eb_rs485`):** Hardware auto-direction default with optional `GPIO21` DE/RE support, `t3.5` inter-frame guard timer, 115,200 bps isolator ceiling warning/clamping, adaptive turnaround margin escalation, FC `0x01`/`0x02`/`0x03`/`0x04`/`0x06` + exception handling, and 16-frame sniffer ring.
- **CAN 2.0B Diagnostics & Bus-Off Recovery (`eb_can`):** `TEC`/`REC`, `ERROR_ACTIVE`/`ERROR_WARNING`/`ERROR_PASSIVE`/`BUS_OFF` tracking, ACK-OK vs Missing-ACK counters, diagnostic hints, and exponential backoff Bus-Off recovery.
- **5-Minute Commissioning Auto-Scanner (`eb_scanner`):** RS-485 baud (`9600..115200`) + slave ID scanner and listen-only CAN bitrate detector (`125k..1M`).
- **Zero-Allocation JSON Profile Engine (`eb_profile`):** Multi-slave RS-485 + CAN decoder supporting up to 5 concurrent profiles with built-in templates (`jk_bms_modbus_v1`, `pylontech_can_v1`, `deye_inverter_modbus_v1`, `growatt_inverter_modbus_v1`).
- **Power-Cut-Safe Circular Storage (`eb_storage`):** Alternating dual superblocks with IEEE CRC-32 (`eb_crc`), crash-replay recovery, corrupt-slot skipping, and LittleFS (`/telemetry_fs`) + POSIX backends.
- **Dual-Bank Config & 60s Watchdog (`eb_config`):** Active + backup banks with 60s confirmation timeout and automatic rollback on reboot if unconfirmed.
- **PSRAM Offload & Ephemeral BLE (`eb_mem`, `eb_ble_prov`):** `.ext_ram.bss` placement for large buffers, SoftAP fallback tracking, and `bt_disable()` post-provisioning shutdown.
- **MQTT FIFO Batcher & Health-Gated OTA (`eb_mqtt`, `eb_ota`):** QoS-gated store-and-forward drain, resumable byte-range OTA staging with CRC-32 verification, and sustained health gate before `boot_write_img_confirmed()`.
- **Web Bus Analyzer & Docs App (`eb_web_analyzer`, `docs/`):** Mobile-first REST API + SPA and GitHub Pages documentation app.
- **Verification Suite (`tests/`):** 6 host test suites compiled with `-Wall -Wextra -Werror -fsanitize=address,undefined`, including a 500-cycle power-cut torture test and a 7-day 3,024,000-record simulation.
- **Licensing & Build BOM (`LICENSE`, `COMMERCIAL_LICENSE.md`, `THIRD_PARTY_LICENSES.md`, `west.yml`):** Dual-licensed under `GPL-3.0-only OR LicenseRef-EdgeBridge-Commercial` with GPLv3-compatible ESP32-S3 Zephyr `v3.7.0` build components (`hal_espressif`, `mcuboot`, `littlefs`, `mbedtls`, `tinycrypt`, `zcbor`).
