# Security Policy — EdgeBridge-S3

## Firmware Signing (MCUboot ECDSA-P256)

EdgeBridge-S3 uses **MCUboot** with **ECDSA-P256** image signature verification (`SB_CONFIG_BOOT_SIGNATURE_TYPE_ECDSA_P256=y`).

- **Never commit production signing private keys (`*.pem`) to source control.** `keys/*.pem` and `*.pem` are excluded via `.gitignore`.
- Use `scripts/gen_dev_keys.sh` only for local engineering evaluation.
- For production/OEM pilot deployments, generate ECDSA-P256 keys in an offline HSM or CI secret store and pass `-DSB_CONFIG_BOOT_SIGNATURE_KEY_FILE=/path/to/prod-ecdsa-p256.pem` at build time.

## Network & Provisioning

- Ephemeral BLE provisioning automatically disables the BLE radio (`bt_disable()`) upon credential receipt or after `CONFIG_EDGEBRIDGE_BLE_PROV_TIMEOUT_SEC` (default 300 s).
- Enable MQTT over TLS (`CONFIG_MQTT_LIB_TLS=y`) with broker CA verification in production deployments.
- Unconfirmed configuration changes automatically roll back after `CONFIG_EDGEBRIDGE_CONFIG_WATCHDOG_SEC` (60 s) or immediately on reboot if a crash occurs before confirmation.

## Reporting a Vulnerability

Please open a private security advisory on GitHub (`https://github.com/nitin-chauhan66/edgebridge/security/advisories`) rather than posting sensitive exploit details in public issues.
