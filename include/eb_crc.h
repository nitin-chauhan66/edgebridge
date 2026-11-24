/*
 * Copyright (c) 2026 Nitin Chauhan / EdgeBridge-S3
 * SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-EdgeBridge-Commercial
 */

#ifndef EB_CRC_H
#define EB_CRC_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Standard IEEE 802.3 CRC-32 (poly 0xEDB88320, init 0xFFFFFFFF, final XOR ~0). */
uint32_t eb_crc32_ieee(const void *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* EB_CRC_H */
