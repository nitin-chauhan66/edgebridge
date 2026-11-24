#ifndef EB_MEM_H
#define EB_MEM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * PSRAM placement macro:
 * Routes large telemetry ring buffers, JSON serialization buffers, and HTTP
 * scratch memory into 8 MB Octal PSRAM on ESP32-S3 Zephyr builds so internal
 * SRAM stays below the PRD 35% ceiling even during Wi-Fi + TLS + BLE setup.
 */
#if defined(__ZEPHYR__) && defined(CONFIG_ESP_SPIRAM)
#define EB_PSRAM_BSS __attribute__((section(".ext_ram.bss")))
#else
#define EB_PSRAM_BSS
#endif

#define EB_INTERNAL_SRAM_TARGET_PCT 35U

typedef struct {
    uint32_t internal_sram_total_bytes;
    uint32_t internal_sram_used_bytes;
    uint32_t psram_total_bytes;
    uint32_t psram_used_bytes;
    bool psram_active;
} eb_mem_stats_t;

static inline uint32_t eb_mem_internal_sram_pct(const eb_mem_stats_t *stats) {
    if (!stats || stats->internal_sram_total_bytes == 0U) {
        return 0U;
    }
    return (uint32_t)(((uint64_t)stats->internal_sram_used_bytes * 100ULL) /
                      stats->internal_sram_total_bytes);
}

static inline bool eb_mem_within_sram_budget(const eb_mem_stats_t *stats) {
    return eb_mem_internal_sram_pct(stats) < EB_INTERNAL_SRAM_TARGET_PCT;
}

#ifdef __cplusplus
}
#endif

#endif /* EB_MEM_H */
