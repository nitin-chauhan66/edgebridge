#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
KEY_FILE="${SB_KEY_FILE:-$ROOT_DIR/keys/mcuboot-dev-ecdsa-p256.pem}"

if [ ! -f "$KEY_FILE" ]; then
    "$SCRIPT_DIR/gen_dev_keys.sh"
fi

west build -b esp32s3_devkitm/esp32s3/procpu --sysbuild "$ROOT_DIR" -- \
    -DDTC_OVERLAY_FILE="$ROOT_DIR/boards/waveshare_esp32s3_rs485_can.overlay" \
    -DSB_CONFIG_BOOT_SIGNATURE_KEY_FILE="\"$KEY_FILE\""
