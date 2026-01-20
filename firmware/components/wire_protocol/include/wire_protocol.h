#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Wire Protocol Frame Format (Little-Endian)
 * ==========================================
 * | proto_ver (u8) | msg_type (u8) | seq (u16) | payload_len (u16) | payload (N) | crc16 (u16) |
 *
 * CRC: CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF)
 * Computed over: proto_ver || msg_type || seq || payload_len || payload
 */

#define WIRE_PROTO_VERSION      0x01
#define WIRE_HEADER_SIZE        6       // proto_ver + msg_type + seq + payload_len
#define WIRE_CRC_SIZE           2
#define WIRE_MAX_PAYLOAD        512     // Max payload size
#define WIRE_MAX_FRAME_SIZE     (WIRE_HEADER_SIZE + WIRE_MAX_PAYLOAD + WIRE_CRC_SIZE)

/* Message Types */
typedef enum {
    MSG_TYPE_TELEMETRY_SNAPSHOT = 0x01,     // ESP -> App (Notify)
    MSG_TYPE_COMMAND            = 0x10,     // App -> ESP (Write)
    MSG_TYPE_COMMAND_ACK        = 0x11,     // ESP -> App (Notify/Indicate)
    MSG_TYPE_EVENT              = 0x20,     // ESP -> App (Notify/Indicate)
} wire_msg_type_t;

/* Command IDs */
typedef enum {
    CMD_SET_RELAY               = 0x0001,
    CMD_SET_RELAY_MASK          = 0x0002,
    CMD_SET_SV                  = 0x0020,
    CMD_SET_MODE                = 0x0021,
    CMD_REQUEST_PV_SV_REFRESH   = 0x0022,
    CMD_REQUEST_SNAPSHOT_NOW    = 0x00F0,
    CMD_CLEAR_WARNINGS          = 0x00F1,
    CMD_CLEAR_LATCHED_ALARMS    = 0x00F2,
    CMD_OPEN_SESSION            = 0x0100,
    CMD_KEEPALIVE               = 0x0101,
    CMD_START_RUN               = 0x0102,
    CMD_STOP_RUN                = 0x0103,
    CMD_ENABLE_SERVICE_MODE     = 0x0110,
    CMD_DISABLE_SERVICE_MODE    = 0x0111,
    CMD_CLEAR_ESTOP             = 0x0112,
    CMD_CLEAR_FAULT             = 0x0113,
} wire_cmd_id_t;

/* Command ACK Status Codes */
typedef enum {
    CMD_STATUS_OK               = 0x00,
    CMD_STATUS_REJECTED_POLICY  = 0x01,     // e.g., lease expired
    CMD_STATUS_INVALID_ARGS     = 0x02,
    CMD_STATUS_BUSY             = 0x03,
    CMD_STATUS_HW_FAULT         = 0x04,
    CMD_STATUS_NOT_READY        = 0x05,
    CMD_STATUS_TIMEOUT          = 0x06,     // RS-485 timeout
} wire_cmd_status_t;

/* Event IDs */
typedef enum {
    EVENT_ESTOP_ASSERTED        = 0x1001,
    EVENT_ESTOP_CLEARED         = 0x1002,
    EVENT_HMI_CONNECTED         = 0x1100,
    EVENT_HMI_DISCONNECTED      = 0x1101,
    EVENT_RUN_STARTED           = 0x1200,
    EVENT_RUN_STOPPED           = 0x1201,
    EVENT_RUN_ABORTED           = 0x1202,
    EVENT_PRECOOL_COMPLETE      = 0x1203,
    EVENT_STATE_CHANGED         = 0x1204,
    EVENT_RS485_DEVICE_ONLINE   = 0x1300,
    EVENT_RS485_DEVICE_OFFLINE  = 0x1301,
    EVENT_ALARM_LATCHED         = 0x1400,
    EVENT_ALARM_CLEARED         = 0x1401,
} wire_event_id_t;

/* Event Severity */
typedef enum {
    EVENT_SEVERITY_INFO         = 0x00,
    EVENT_SEVERITY_WARN         = 0x01,
    EVENT_SEVERITY_ALARM        = 0x02,
    EVENT_SEVERITY_CRITICAL     = 0x03,
} wire_event_severity_t;

/* Alarm Bits (alarm_bits u32) */
#define ALARM_BIT_ESTOP_ACTIVE          (1 << 0)
#define ALARM_BIT_DOOR_INTERLOCK        (1 << 1)
#define ALARM_BIT_OVER_TEMP             (1 << 2)
#define ALARM_BIT_RS485_FAULT           (1 << 3)
#define ALARM_BIT_POWER_FAULT           (1 << 4)
#define ALARM_BIT_HMI_NOT_LIVE          (1 << 5)
#define ALARM_BIT_PID1_FAULT            (1 << 6)
#define ALARM_BIT_PID2_FAULT            (1 << 7)
#define ALARM_BIT_PID3_FAULT            (1 << 8)

/* Controller Modes */
typedef enum {
    CTRL_MODE_STOP              = 0x00,
    CTRL_MODE_MANUAL            = 0x01,
    CTRL_MODE_AUTO              = 0x02,
    CTRL_MODE_PROGRAM           = 0x03,
} wire_ctrl_mode_t;

/* Frame header structure */
typedef struct __attribute__((packed)) {
    uint8_t  proto_ver;
    uint8_t  msg_type;
    uint16_t seq;
    uint16_t payload_len;
} wire_frame_header_t;

/* Telemetry snapshot payload (basic - backwards compatible) */
typedef struct __attribute__((packed)) {
    uint32_t timestamp_ms;
    uint16_t di_bits;           // DI1..DI8 in bits 0..7
    uint16_t ro_bits;           // RO1..RO8 in bits 0..7
    uint32_t alarm_bits;
    uint8_t  controller_count;  // 0..3
    // Followed by controller_count * controller_data_t
} wire_telemetry_header_t;

/* Extended telemetry with machine state (appended after controllers) */
typedef struct __attribute__((packed)) {
    uint8_t  machine_state;     // machine_state_t enum value
    uint32_t run_elapsed_ms;    // Time since run started (0 if not running)
    uint32_t run_remaining_ms;  // Time until run completes (0 if no target)
    int16_t  target_temp_x10;   // Current target temperature × 10
    uint8_t  recipe_step;       // Current recipe step (0-based)
    uint8_t  interlock_bits;    // Which interlocks are blocking start
} wire_telemetry_run_state_t;

typedef struct __attribute__((packed)) {
    uint8_t  controller_id;     // 1, 2, or 3
    int16_t  pv_x10;            // Process Variable × 10
    int16_t  sv_x10;            // Setpoint Value × 10
    uint16_t op_x10;            // Output % × 10
    uint8_t  mode;              // wire_ctrl_mode_t
    uint16_t age_ms;            // How fresh the RS-485 sample is
} wire_controller_data_t;

/* Command payload header */
typedef struct __attribute__((packed)) {
    uint16_t cmd_id;
    uint16_t flags;             // Reserved, set to 0
    // Followed by command-specific payload
} wire_cmd_header_t;

/* Command ACK payload */
typedef struct __attribute__((packed)) {
    uint16_t acked_seq;
    uint16_t cmd_id;
    uint8_t  status;            // wire_cmd_status_t
    uint16_t detail;            // Subcode
    // Followed by optional_data
} wire_cmd_ack_t;

/* OPEN_SESSION command payload */
typedef struct __attribute__((packed)) {
    uint32_t client_nonce;
} wire_cmd_open_session_t;

/* OPEN_SESSION ACK optional data */
typedef struct __attribute__((packed)) {
    uint32_t session_id;
    uint16_t lease_ms;
} wire_ack_open_session_t;

/* KEEPALIVE command payload */
typedef struct __attribute__((packed)) {
    uint32_t session_id;
} wire_cmd_keepalive_t;

/* Event payload */
typedef struct __attribute__((packed)) {
    uint16_t event_id;
    uint8_t  severity;          // wire_event_severity_t
    uint8_t  source;            // 0=SYSTEM, 1..3=controller_id
    // Followed by event-specific data
} wire_event_header_t;

/*
 * CRC-16/CCITT-FALSE
 * Poly: 0x1021, Init: 0xFFFF, RefIn: false, RefOut: false, XorOut: 0x0000
 */
uint16_t wire_crc16(const uint8_t *data, size_t len);

/*
 * Build a complete frame with header, payload, and CRC.
 * Returns total frame length, or 0 on error.
 */
size_t wire_build_frame(
    uint8_t *out_buf,
    size_t out_buf_size,
    uint8_t msg_type,
    uint16_t seq,
    const uint8_t *payload,
    uint16_t payload_len
);

/*
 * Parse a received frame. Validates CRC.
 * Returns true if valid, false otherwise.
 * On success, header is filled and payload_out points into frame_buf.
 */
bool wire_parse_frame(
    const uint8_t *frame_buf,
    size_t frame_len,
    wire_frame_header_t *header,
    const uint8_t **payload_out
);

/*
 * Build a COMMAND_ACK frame.
 */
size_t wire_build_cmd_ack(
    uint8_t *out_buf,
    size_t out_buf_size,
    uint16_t seq,
    uint16_t acked_seq,
    uint16_t cmd_id,
    uint8_t status,
    uint16_t detail,
    const uint8_t *optional_data,
    size_t optional_len
);

/*
 * Build a TELEMETRY_SNAPSHOT frame.
 */
size_t wire_build_telemetry(
    uint8_t *out_buf,
    size_t out_buf_size,
    uint16_t seq,
    uint32_t timestamp_ms,
    uint16_t di_bits,
    uint16_t ro_bits,
    uint32_t alarm_bits,
    const wire_controller_data_t *controllers,
    uint8_t controller_count
);

/*
 * Build a TELEMETRY_SNAPSHOT frame with extended machine state.
 */
size_t wire_build_telemetry_ext(
    uint8_t *out_buf,
    size_t out_buf_size,
    uint16_t seq,
    uint32_t timestamp_ms,
    uint16_t di_bits,
    uint16_t ro_bits,
    uint32_t alarm_bits,
    const wire_controller_data_t *controllers,
    uint8_t controller_count,
    const wire_telemetry_run_state_t *run_state
);

/*
 * Build an EVENT frame.
 */
size_t wire_build_event(
    uint8_t *out_buf,
    size_t out_buf_size,
    uint16_t seq,
    uint16_t event_id,
    uint8_t severity,
    uint8_t source,
    const uint8_t *event_data,
    size_t event_data_len
);

#ifdef __cplusplus
}
#endif
