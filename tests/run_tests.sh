#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_BIN="/tmp/edgebridge_phase0_tests"

gcc -std=c11 -Wall -Wextra -Werror -O2 -g \
    -fsanitize=address,undefined \
    -I"$ROOT_DIR/include" \
    "$ROOT_DIR/src/eb_crc.c" \
    "$ROOT_DIR/src/eb_config.c" \
    "$ROOT_DIR/src/eb_ring_queue.c" \
    "$ROOT_DIR/src/eb_rs485.c" \
    "$ROOT_DIR/src/eb_can.c" \
    "$ROOT_DIR/src/eb_scanner.c" \
    "$ROOT_DIR/src/eb_profile.c" \
    "$ROOT_DIR/src/eb_storage.c" \
    "$ROOT_DIR/src/eb_mqtt.c" \
    "$ROOT_DIR/src/eb_ble_prov.c" \
    "$ROOT_DIR/src/eb_web_analyzer.c" \
    "$ROOT_DIR/src/eb_ota.c" \
    "$SCRIPT_DIR/test_runner.c" \
    -o "$BUILD_BIN"

"$BUILD_BIN"
