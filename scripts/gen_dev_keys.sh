#!/usr/bin/env bash
set -euo pipefail

# Generate a local ECDSA-P256 development signing key for MCUboot sysbuild.
# Do NOT commit keys/*.pem to source control.
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
KEY_DIR="$ROOT_DIR/keys"
KEY_FILE="$KEY_DIR/mcuboot-dev-ecdsa-p256.pem"

mkdir -p "$KEY_DIR"
if [ -f "$KEY_FILE" ]; then
    echo "Dev key already exists at $KEY_FILE"
    exit 0
fi

openssl ecparam -name prime256v1 -genkey -noout -out "$KEY_FILE"
chmod 600 "$KEY_FILE"
echo "Generated MCUboot dev ECDSA-P256 key: $KEY_FILE"
