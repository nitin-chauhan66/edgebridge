#include "eb_crc.h"

uint32_t eb_crc32_ieee(const void *data, size_t len) {
    const uint8_t *bytes = (const uint8_t *)data;
    if (!bytes && len > 0U) {
        return 0U;
    }
    uint32_t crc = 0xFFFFFFFFU;
    for (size_t i = 0U; i < len; i++) {
        crc ^= bytes[i];
        for (int b = 0; b < 8; b++) {
            uint32_t mask = (uint32_t)(-(int32_t)(crc & 1U));
            crc = (crc >> 1) ^ (0xEDB88320U & mask);
        }
    }
    return ~crc;
}
