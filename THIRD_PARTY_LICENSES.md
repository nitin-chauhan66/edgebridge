# Third-Party Build Components & License Alignment

All build-time dependencies pinned in [west.yml](west.yml) are scoped strictly to the **Waveshare ESP32-S3-RS485-CAN (Xtensa LX7)** Phase 0 target and verified directly against upstream repositories for **GNU GPL v3.0** (`GPL-3.0-only`) compatibility and attribution compliance:

| Component | Source / Module | Verified Upstream License | GPL-3.0 Compatibility | Role in EdgeBridge-S3 |
|---|---|---|---|---|
| **Zephyr RTOS (`v3.7.0`)** | `zephyrproject-rtos/zephyr` | `Apache-2.0` | Compatible | Deterministic 3-thread kernel, UART, TWAI CAN, USB CDC-ACM, Wi-Fi/MQTT, NVS |
| **Espressif HAL** | `hal_espressif` | `Apache-2.0` (`zephyr/blobs/license.txt` also `Apache-2.0`) | Compatible + Sec. 7 Exception | ESP32-S3 drivers, 8 MB Octal PSRAM init, Wi-Fi/BLE PHY libraries |
| **MCUboot** | `mcuboot` | `Apache-2.0` | Compatible | Dual-slot `slot0`/`slot1` bootloader with ECDSA-P256 verification & rollback |
| **Mbed TLS** | `mbedtls` | `Apache-2.0 OR GPL-2.0-or-later` | Compatible | MQTT TLS 1.2/1.3 & crypto primitives (heap offloaded to PSRAM) |
| **zcbor** | `zcbor` | `Apache-2.0` | Compatible | Compact CBOR codec used by Zephyr image management / MCUboot |
| **LittleFS** | `littlefs` | `BSD-3-Clause` | Compatible | Wear-leveled power-cut-safe filesystem for `/telemetry_fs` |
| **picolibc** | `picolibc` | `BSD-3-Clause` (`BSD3-1`) | Compatible | Default C runtime library in Zephyr v3.7.0 |
| **TinyCrypt (`0.2.8`)** | `tinycrypt` | `BSD-3-Clause` (Intel) + `BSD-2-Clause` (`micro-ecc`) | Compatible | Lightweight SHA-256 / ECC primitives used by MCUboot ECDSA-P256 |

### Compliance Artifacts Included

- [LICENSE](LICENSE) — GNU GPL v3.0 (`GPL-3.0-only`)
- [COMMERCIAL_LICENSE.md](COMMERCIAL_LICENSE.md) — Commercial OEM licensing & GPLv3 Section 7 permission for Espressif SoC RF binary blobs
- [LICENSES/Apache-2.0.txt](LICENSES/Apache-2.0.txt) — Full Apache License 2.0 text satisfying Apache-2.0 Section 4(a) redistribution requirements
- [NOTICE](NOTICE) — Upstream copyright and attribution notices satisfying Apache-2.0 Section 4(d) and BSD-2/3-Clause notice conditions

> Unused non-ESP32 modules (such as ARM `cmsis` and `segger` RTT) and unnecessary Wi-Fi supplicant modules (`hostap`, not needed by `CONFIG_WIFI_ESP32`) are excluded from `west.yml`.
