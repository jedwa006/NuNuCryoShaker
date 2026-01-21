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
    /* I/O Control (0x0001 - 0x000F) */
    CMD_SET_RELAY               = 0x0001,
    CMD_SET_RELAY_MASK          = 0x0002,

    /* PID Controller Commands (0x0020 - 0x003F) */
    CMD_SET_SV                  = 0x0020,   /* Set temperature setpoint */
    CMD_SET_MODE                = 0x0021,   /* Set controller mode */
    CMD_REQUEST_PV_SV_REFRESH   = 0x0022,   /* Force immediate poll */
    CMD_SET_PID_PARAMS          = 0x0023,   /* Write P, I, D tuning */
    CMD_READ_PID_PARAMS         = 0x0024,   /* Read P, I, D tuning */
    CMD_START_AUTOTUNE          = 0x0025,   /* Start auto-tune cycle */
    CMD_STOP_AUTOTUNE           = 0x0026,   /* Stop auto-tune cycle */
    CMD_SET_ALARM_LIMITS        = 0x0027,   /* Set alarm setpoints */
    CMD_READ_ALARM_LIMITS       = 0x0028,   /* Read alarm setpoints */
    CMD_READ_REGISTERS          = 0x0030,   /* Read Modbus registers */
    CMD_WRITE_REGISTER          = 0x0031,   /* Write single Modbus register */

    /* Configuration Commands (0x0040 - 0x004F) */
    CMD_SET_IDLE_TIMEOUT        = 0x0040,   /* Set lazy polling idle timeout */
    CMD_GET_IDLE_TIMEOUT        = 0x0041,   /* Get lazy polling idle timeout */

    /* Diagnostics (0x00F0 - 0x00FF) */
    CMD_REQUEST_SNAPSHOT_NOW    = 0x00F0,
    CMD_CLEAR_WARNINGS          = 0x00F1,
    CMD_CLEAR_LATCHED_ALARMS    = 0x00F2,

    /* Session Management (0x0100 - 0x010F) */
    CMD_OPEN_SESSION            = 0x0100,
    CMD_KEEPALIVE               = 0x0101,
    CMD_START_RUN               = 0x0102,
    CMD_STOP_RUN                = 0x0103,
    CMD_PAUSE_RUN               = 0x0104,   /* Pause run (door unlock, motor stop) */
    CMD_RESUME_RUN              = 0x0105,   /* Resume from pause */

    /* Service Mode (0x0110 - 0x011F) */
    CMD_ENABLE_SERVICE_MODE     = 0x0110,
    CMD_DISABLE_SERVICE_MODE    = 0x0111,
    CMD_CLEAR_ESTOP             = 0x0112,
    CMD_CLEAR_FAULT             = 0x0113,

    /* Safety Gate Commands (0x0070 - 0x007F) */
    CMD_GET_CAPABILITIES        = 0x0070,   /* Get subsystem capability levels */
    CMD_SET_CAPABILITY          = 0x0071,   /* Set subsystem capability level */
    CMD_GET_SAFETY_GATES        = 0x0072,   /* Get safety gate enable/status */
    CMD_SET_SAFETY_GATE         = 0x0073,   /* Enable/bypass a safety gate */
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
    EVENT_RUN_PAUSED            = 0x1205,
    EVENT_RUN_RESUMED           = 0x1206,
    EVENT_RS485_DEVICE_ONLINE   = 0x1300,
    EVENT_RS485_DEVICE_OFFLINE  = 0x1301,
    EVENT_ALARM_LATCHED         = 0x1400,
    EVENT_ALARM_CLEARED         = 0x1401,
    EVENT_AUTOTUNE_STARTED      = 0x1500,
    EVENT_AUTOTUNE_COMPLETE     = 0x1501,
    EVENT_AUTOTUNE_FAILED       = 0x1502,
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
/* Safety Gate Extension (bits 9-15) */
#define ALARM_BIT_GATE_DOOR_BYPASSED    (1 << 9)    /* Door gate is bypassed */
#define ALARM_BIT_GATE_HMI_BYPASSED     (1 << 10)   /* HMI gate is bypassed */
#define ALARM_BIT_GATE_PID_BYPASSED     (1 << 11)   /* Any PID gate bypassed */
#define ALARM_BIT_PID1_PROBE_ERROR      (1 << 12)   /* PID1 has probe error (HHHH/LLLL) */
#define ALARM_BIT_PID2_PROBE_ERROR      (1 << 13)   /* PID2 has probe error (HHHH/LLLL) */
#define ALARM_BIT_PID3_PROBE_ERROR      (1 << 14)   /* PID3 has probe error (HHHH/LLLL) */

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
    uint8_t  machine_state;     // machine_state_t enum value (offset 0)
    uint32_t run_elapsed_ms;    // Time since run started (offset 1)
    uint32_t run_remaining_ms;  // Time until run completes (offset 5)
    int16_t  target_temp_x10;   // Current target temperature × 10 (offset 9)
    uint8_t  recipe_step;       // Current recipe step (offset 11)
    uint8_t  interlock_bits;    // Which interlocks are blocking start (offset 12)
    uint8_t  lazy_poll_active;  // 1 if lazy polling active (offset 13)
    uint8_t  idle_timeout_min;  // Idle timeout in minutes, 0=disabled (offset 14)
    uint8_t  reserved;          // Reserved for future use (offset 15)
} wire_telemetry_run_state_t;   // Total: 16 bytes

typedef struct __attribute__((packed)) {
    uint8_t  controller_id;     // 1, 2, or 3
    int16_t  pv_x10;            // Process Variable × 10
    int16_t  sv_x10;            // Setpoint Value × 10
    uint16_t op_x10;            // Output % × 10
    uint8_t  mode;              // wire_ctrl_mode_t
    uint16_t age_ms;            // How fresh the RS-485 sample is
} wire_controller_data_t;

/* Extended controller data with status flags */
typedef struct __attribute__((packed)) {
    uint8_t  controller_id;     // 1, 2, or 3
    int16_t  pv_x10;            // Process Variable × 10
    int16_t  sv_x10;            // Setpoint Value × 10
    uint16_t op_x10;            // Output % × 10
    uint8_t  mode;              // wire_ctrl_mode_t
    uint16_t age_ms;            // How fresh the RS-485 sample is
    uint8_t  status_flags;      // PID status (alarm1, alarm2, autotune, etc.)
} wire_controller_data_ext_t;

/* Controller status flags */
#define CTRL_STATUS_ALARM1      (1 << 0)    /* Alarm 1 active */
#define CTRL_STATUS_ALARM2      (1 << 1)    /* Alarm 2 active */
#define CTRL_STATUS_OUTPUT1     (1 << 2)    /* Output 1 active */
#define CTRL_STATUS_OUTPUT2     (1 << 3)    /* Output 2 active */
#define CTRL_STATUS_AUTOTUNE    (1 << 4)    /* Auto-tune in progress */
#define CTRL_STATUS_STALE       (1 << 5)    /* Data is stale */
#define CTRL_STATUS_OFFLINE     (1 << 6)    /* Controller offline */

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

/* SET_SV command payload */
typedef struct __attribute__((packed)) {
    uint8_t  controller_id;     /* 1, 2, or 3 */
    int16_t  sv_x10;            /* Setpoint × 10 */
} wire_cmd_set_sv_t;

/* SET_MODE command payload */
typedef struct __attribute__((packed)) {
    uint8_t  controller_id;     /* 1, 2, or 3 */
    uint8_t  mode;              /* wire_ctrl_mode_t */
} wire_cmd_set_mode_t;

/* SET_PID_PARAMS command payload */
typedef struct __attribute__((packed)) {
    uint8_t  controller_id;     /* 1, 2, or 3 */
    int16_t  p_gain_x10;        /* P gain × 10 */
    uint16_t i_time;            /* I time in seconds */
    uint16_t d_time;            /* D time in seconds */
} wire_cmd_set_pid_params_t;

/* READ_PID_PARAMS ACK optional data */
typedef struct __attribute__((packed)) {
    uint8_t  controller_id;
    int16_t  p_gain_x10;
    uint16_t i_time;
    uint16_t d_time;
} wire_ack_pid_params_t;

/* START/STOP_AUTOTUNE command payload */
typedef struct __attribute__((packed)) {
    uint8_t  controller_id;     /* 1, 2, or 3 */
} wire_cmd_autotune_t;

/* SET_ALARM_LIMITS command payload */
typedef struct __attribute__((packed)) {
    uint8_t  controller_id;     /* 1, 2, or 3 */
    int16_t  alarm1_x10;        /* Alarm 1 setpoint × 10 */
    int16_t  alarm2_x10;        /* Alarm 2 setpoint × 10 */
} wire_cmd_set_alarm_limits_t;

/* READ_ALARM_LIMITS ACK optional data */
typedef struct __attribute__((packed)) {
    uint8_t  controller_id;
    int16_t  alarm1_x10;
    int16_t  alarm2_x10;
} wire_ack_alarm_limits_t;

/* READ_REGISTERS command payload */
typedef struct __attribute__((packed)) {
    uint8_t  controller_id;     /* 1, 2, or 3 */
    uint16_t start_address;     /* Starting register address */
    uint8_t  count;             /* Number of registers (1-16) */
} wire_cmd_read_registers_t;

/* READ_REGISTERS ACK optional data (variable size) */
typedef struct __attribute__((packed)) {
    uint8_t  controller_id;
    uint16_t start_address;
    uint8_t  count;
    /* Followed by count × uint16_t values (little-endian) */
} wire_ack_read_registers_t;

/* WRITE_REGISTER command payload */
typedef struct __attribute__((packed)) {
    uint8_t  controller_id;     /* 1, 2, or 3 */
    uint16_t address;           /* Register address */
    uint16_t value;             /* Value to write */
} wire_cmd_write_register_t;

/* WRITE_REGISTER ACK optional data */
typedef struct __attribute__((packed)) {
    uint8_t  controller_id;
    uint16_t address;
    uint16_t value;             /* Verified value after read-back */
} wire_ack_write_register_t;

/* Event payload */
typedef struct __attribute__((packed)) {
    uint16_t event_id;
    uint8_t  severity;          // wire_event_severity_t
    uint8_t  source;            // 0=SYSTEM, 1..3=controller_id
    // Followed by event-specific data
} wire_event_header_t;

/* GET_CAPABILITIES ACK optional data */
typedef struct __attribute__((packed)) {
    uint8_t  pid1_cap;          /* PID1 capability (0=NOT_PRESENT, 1=OPTIONAL, 2=REQUIRED) */
    uint8_t  pid2_cap;          /* PID2 capability */
    uint8_t  pid3_cap;          /* PID3 capability */
    uint8_t  di1_cap;           /* E-Stop DI capability (always 2=REQUIRED) */
    uint8_t  di2_cap;           /* Door sensor capability */
    uint8_t  di3_cap;           /* LN2 sensor capability */
    uint8_t  di4_cap;           /* Motor fault capability */
    uint8_t  reserved;          /* Reserved for expansion */
} wire_ack_capabilities_t;

/* SET_CAPABILITY command payload */
typedef struct __attribute__((packed)) {
    uint8_t  subsystem_id;      /* Subsystem ID (0-6) */
    uint8_t  capability;        /* Capability level (0-2) */
} wire_cmd_set_capability_t;

/* GET_SAFETY_GATES ACK optional data */
typedef struct __attribute__((packed)) {
    uint16_t gate_enable;       /* Bitmask: 1=gate enabled, 0=bypassed (LE u16) */
    uint16_t gate_status;       /* Bitmask: 1=gate passing, 0=blocking (LE u16) */
} wire_ack_safety_gates_t;

/* SET_SAFETY_GATE command payload */
typedef struct __attribute__((packed)) {
    uint8_t  gate_id;           /* Gate ID (0-9) */
    uint8_t  enabled;           /* 1=enable gate, 0=bypass gate */
} wire_cmd_set_safety_gate_t;

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
