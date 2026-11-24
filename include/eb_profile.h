#ifndef EB_PROFILE_H
#define EB_PROFILE_H

#include "eb_can.h"
#include "eb_ring_queue.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EB_PROFILE_MAX_METRICS     16U
#define EB_PROFILE_MAX_ACTIVE      5U   /* e.g. 4 Modbus slaves + 1 CAN BMS */
#define EB_PROFILE_NAME_LEN        32U
#define EB_PROFILE_UNIT_LEN        8U
#define EB_SNAPSHOT_MAX_VALUES     32U

typedef struct {
    char name[EB_PROFILE_NAME_LEN];
    char unit[EB_PROFILE_UNIT_LEN];
    uint8_t slave_id;      /* 0 => inherit profile slave_id */
    uint8_t func;          /* Modbus function 1..4 */
    uint16_t address;      /* Modbus register address */
    uint8_t length;        /* Register count (1 or 2) or CAN bytes (1,2,4) */
    uint32_t can_id;       /* CAN 11/29-bit ID if bus == EB_BUS_CAN */
    uint8_t byte_offset;   /* CAN payload start byte (0..7) */
    bool is_signed;
    bool little_endian;    /* CAN: true for Pylontech/Intel LE; Modbus: false=BE */
    double scale;
} eb_signal_def_t;

typedef struct {
    char profile_id[EB_PROFILE_NAME_LEN];
    char device_name[48];
    eb_bus_type_t bus;
    uint32_t baud_or_bitrate;
    uint8_t slave_id;
    uint16_t poll_interval_ms;
    uint32_t turnaround_guard_us;
    size_t signal_count;
    eb_signal_def_t signals[EB_PROFILE_MAX_METRICS];
} eb_device_profile_t;

typedef struct {
    char profile_id[EB_PROFILE_NAME_LEN];
    char metric_name[EB_PROFILE_NAME_LEN];
    char unit[EB_PROFILE_UNIT_LEN];
    uint8_t bus;
    uint8_t addr;
    uint32_t can_id;
    double value;
    uint64_t timestamp_ms;
    bool valid;
} eb_decoded_value_t;

typedef struct {
    eb_device_profile_t active_profiles[EB_PROFILE_MAX_ACTIVE];
    size_t profile_count;
    eb_decoded_value_t latest[EB_SNAPSHOT_MAX_VALUES];
    size_t latest_count;
} eb_profile_mgr_t;

void eb_profile_mgr_init(eb_profile_mgr_t *mgr);

/* Load a built-in profile by ID (e.g. "jk_bms_modbus_v1", "pylontech_can_v1", "deye_inverter_modbus_v1"). */
int eb_profile_load_builtin(eb_profile_mgr_t *mgr, const char *profile_id, uint8_t slave_override);

/* Parse and register a custom JSON profile matching the profiles JSON schema. */
int eb_profile_load_json(eb_profile_mgr_t *mgr, const char *json_str, size_t json_len);

/* Clear all active profiles and decoded snapshot values. */
void eb_profile_clear(eb_profile_mgr_t *mgr);

/* Number of built-in profiles compiled into firmware. */
size_t eb_profile_builtin_count(void);
const char *eb_profile_builtin_id(size_t idx);
const char *eb_profile_builtin_json(size_t idx);

/* Decode a Modbus reply payload (data bytes after byte_count) for a specific signal. */
int eb_profile_decode_modbus_signal(eb_profile_mgr_t *mgr,
                                    const eb_device_profile_t *prof,
                                    const eb_signal_def_t *sig,
                                    const uint8_t *reg_bytes,
                                    size_t byte_len,
                                    uint64_t timestamp_ms,
                                    eb_decoded_value_t *out_val);

/* Decode any matching signals in active CAN profiles for a received CAN frame. */
size_t eb_profile_decode_can_frame(eb_profile_mgr_t *mgr,
                                   const eb_can_frame_t *frame,
                                   uint64_t timestamp_ms);

#ifdef __cplusplus
}
#endif

#endif /* EB_PROFILE_H */
