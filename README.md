# EdgeBridge-S3 — Phase 0 Firmware (`v0.1.0-phase0`)

[![EdgeBridge-S3 Phase 0 CI](https://github.com/nitin-chauhan66/edgebridge/actions/workflows/ci.yml/badge.svg)](https://github.com/nitin-chauhan66/edgebridge/actions/workflows/ci.yml)
[![Deploy EdgeBridge-S3 Docs to GitHub Pages](https://github.com/nitin-chauhan66/edgebridge/actions/workflows/pages.yml/badge.svg)](https://nitin-chauhan66.github.io/edgebridge/)

**Interactive Documentation App:** https://nitin-chauhan66.github.io/edgebridge/

**EdgeBridge-S3** is a self-healing RS-485 & CAN edge gateway with a built-in Web Bus Analyzer, built on **Zephyr RTOS + MCUboot + LittleFS** for the **ESP32-S3R8 (16 MB Flash + 8 MB Octal PSRAM)**.

Phase 0 targets **zero-capex deployment** on the off-the-shelf [Waveshare ESP32-S3-RS485-CAN](https://www.waveshare.com/wiki/ESP32-S3-RS485-CAN) DIN-rail board, solving every documented hardware and field quirk directly in firmware.

## Hardware Pinout (Waveshare ESP32-S3-RS485-CAN)

| Interface | Signal | ESP32-S3 GPIO | Notes |
|---|---|---|---|
| **RS-485** | TX | `GPIO17` | Isolated half-duplex transceiver |
| **RS-485** | RX | `GPIO18` | Isolated half-duplex transceiver (hardware auto-direction default) |
| **RS-485** | Optional DE/EN | `GPIO21` | Optional manual DE/RE control (`rs485_auto_direction=false`) |
| **CAN (TWAI)** | TX | `GPIO15` | Isolated CAN 2.0B transceiver |
| **CAN (TWAI)** | RX | `GPIO16` | Isolated CAN 2.0B transceiver |
| **USB-C** | D- / D+ | `GPIO19` / `GPIO20` | Native USB CDC-ACM console with deferred non-blocking logging |

> **Field Wiring Reminders (Phase 0 Hardware):**
> - **120 Ω Termination:** On the Waveshare board, open the enclosure and set the `RS485 120R` / `CAN 120R` jumper caps to `120R` if this node is a bus endpoint, or `NC` if mid-bus.
> - **External Antenna:** Switching between ceramic and IPEX1 on the Waveshare PCB requires rotating the 0402/0603 RF resistor.
> - **USB-C Flashing Trap:** Hold `BOOT`, tap `RESET`, release `RESET`, then release `BOOT` if the board does not auto-enter download mode.

## Flash Partition Layout (16 MB)

| Offset | Size | Partition | Purpose |
|---|---|---|---|
| `0x000000` | 64 KB | `boot_partition` | MCUboot bootloader (ECDSA-P256) |
| `0x010000` | 4 MB | `slot0_partition` | Primary application image |
| `0x410000` | 4 MB | `slot1_partition` | Secondary OTA staging image |
| `0x810000` | 256 KB | `scratch_partition` | MCUboot swap metadata |
| `0x850000` | 512 KB | `nvs_config` | Dual-bank active + backup config with boot rollback |
| `0x8D0000` | 7.1 MB | `telemetry_fs` | LittleFS circular store-and-forward log (dual superblocks) |

## Architecture Overview

1. **Deterministic 3-Thread Runtime (`src/main.c`)**
   - **Thread 1 (High Priority — Bus Poller):** Executes multi-slave Modbus RTU schedules (`eb_rs485.c`, `eb_profile.c`) with adaptive `t3.5` inter-frame guard timers and CAN 2.0B frame decoding/recovery (`eb_can.c`); pushes records into a lock-free SPSC ring queue (`eb_ring_queue.c`).
   - **Thread 2 (Medium Priority — Storage & MQTT Batcher):** Drains the ring queue to the LittleFS dual-superblock circular store-and-forward log (`eb_storage.c`) and publishes FIFO batches via MQTT (`eb_mqtt.c`), acknowledging slots only after publish confirmation.
   - **Thread 3 (Low Priority — Web Bus Analyzer, Config Watchdog & OTA):** Serves the mobile-first Web Bus Analyzer & REST API (`eb_web_analyzer.c`), enforces the 60s dual-bank config rollback watchdog (`eb_config.c`), runs ephemeral BLE/AP provisioning (`eb_ble_prov.c`), gates MCUboot slot confirmation on sustained network + bus health (`eb_ota.c`), and feeds the hardware watchdog.
2. **Hardened Quirks Solved**
   - **RS-485 Auto-Direction Clipping:** Enforces `t3.5` turnaround delay (minimum 1,750 µs at >19.2 kbps), caps/warns above `115,200 bps` isolator propagation limit, flushes stale RX noise, and auto-escalates turnaround margin on consecutive clipped replies.
   - **BLE + Wi-Fi SRAM Crashes:** Routes ring queues, profile tables, HTTP/MQTT buffers to 8 MB Octal PSRAM (`include/eb_mem.h`, `.ext_ram.bss`) and shuts down BLE (`bt_disable()`) immediately after provisioning or timeout.
   - **CAN LED Ambiguity:** Tracks `ERROR_ACTIVE` / `ERROR_WARNING` / `ERROR_PASSIVE` / `BUS_OFF`, `TEC`/`REC`, ACK-OK vs Missing-ACK counters, and exponential Bus-Off recovery in the Web Bus Analyzer.
   - **Power-Cut Resilience:** Dual alternating superblocks + per-record IEEE CRC-32 (`eb_crc.c`) + crash-replay recovery skip half-written slots without corrupting the ring.
   - **60s Config Watchdog & Crash Rollback:** Unconfirmed network/bus config changes auto-revert after 60 seconds OR immediately on reboot if a crash occurs prior to confirmation.

## Web Bus Analyzer REST API (`include/eb_web_analyzer.h`)

- `GET /` — Mobile-first Bus Analyzer & 5-minute commissioning UI
- `GET /api/v1/diagnostics` — Firmware version, RS-485, CAN, LittleFS, Config Watchdog, MQTT, OTA, and SRAM/PSRAM stats
- `GET /api/v1/telemetry` — Live decoded BMS/inverter engineering values + recent RS-485/CAN frames
- `GET|POST /api/v1/scan` — RS-485 baud/slave-ID & listen-only CAN bitrate auto-scanner (`eb_scanner.c`)
- `GET|POST /api/v1/profiles` — Built-in (`jk_bms_modbus_v1`, `pylontech_can_v1`, `deye_inverter_modbus_v1`, `growatt_inverter_modbus_v1`) & custom JSON profiles
- `GET|POST /api/v1/config` & `POST /api/v1/config/confirm` — Stage & confirm configuration
- `GET /api/v1/ota` — MCUboot OTA staging and confirmation status

## Building & Running Tests

### Host Verification Suite (`-Wall -Wextra -Werror -fsanitize=address,undefined`)

```bash
cd tests
chmod +x run_tests.sh
./run_tests.sh
```

### Zephyr RTOS + MCUboot Firmware (`west.yml`)

```bash
# 1. Initialize west workspace (first time)
west init -l .
west update
west blobs fetch hal_espressif

# 2. Generate a local dev signing key (NEVER commit production keys!) and build
chmod +x scripts/gen_dev_keys.sh scripts/build_phase0.sh
./scripts/build_phase0.sh
west flash
```

## Licensing (Dual-License: GNU GPL v3.0 + Commercial OEM)

EdgeBridge-S3 is dual-licensed (`GPL-3.0-only OR LicenseRef-EdgeBridge-Commercial`):

- **Open Source (`GPL-3.0-only`):** Free for developers, installers, and open-source GPLv3 projects (with a GPLv3 Section 7 permission for unmodified Espressif SoC radio blobs in `hal_espressif`).
- **Commercial OEM License:** Organizations embedding EdgeBridge-S3 into proprietary or closed-source commercial products require a Commercial License — see [COMMERCIAL_LICENSE.md](COMMERCIAL_LICENSE.md).

## Release, Components & Governance

- [CHANGELOG.md](CHANGELOG.md) — Release history (`v0.1.0-phase0`)
- [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md) — Aligned ESP32-S3 build BOM (`zephyr v3.7.0`, `hal_espressif`, `mcuboot`, `littlefs`, `mbedtls`, `picolibc`, `tinycrypt`, `zcbor`)
- [NOTICE](NOTICE) & [LICENSES/Apache-2.0.txt](LICENSES/Apache-2.0.txt) — Upstream Apache-2.0 & BSD attributions
- [COMMERCIAL_LICENSE.md](COMMERCIAL_LICENSE.md) — Dual-licensing & OEM terms
- [SECURITY.md](SECURITY.md) — MCUboot ECDSA-P256 key handling & reporting policy
- [LICENSE](LICENSE) — GNU General Public License v3.0
