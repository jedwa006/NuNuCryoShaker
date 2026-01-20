#include "ble_gatt.h"
#include "wire_protocol.h"
#include "session_mgr.h"
#include "telemetry.h"
#include "relay_ctrl.h"
#include "status_led.h"
#include "machine_state.h"
#include "pid_controller.h"

#include <string.h>
#include <stdio.h>

#include "esp_log.h"
#include "esp_mac.h"
#include "nvs_flash.h"

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

static const char *TAG = "ble_gatt";

/* Connection state */
static uint16_t s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static bool s_telemetry_subscribed = false;
static bool s_events_notify_subscribed = false;
static bool s_events_indicate_subscribed = false;

/* Characteristic value handles */
static uint16_t s_device_info_handle;
static uint16_t s_telemetry_handle;
static uint16_t s_command_rx_handle;
static uint16_t s_events_acks_handle;

/* Sequence counter for outgoing messages */
static uint16_t s_tx_seq = 0;

/* Device name with MAC suffix */
static char s_device_name[20];

/* Service UUID: F0C5B4D2-3D1E-4A27-9B8A-2F0B3C4D5E60 */
static const ble_uuid128_t svc_uuid = BLE_UUID128_INIT(
    0x60, 0x5E, 0x4D, 0x3C, 0x0B, 0x2F, 0x8A, 0x9B,
    0x27, 0x4A, 0x1E, 0x3D, 0xD2, 0xB4, 0xC5, 0xF0
);

/* Characteristic UUIDs */
static const ble_uuid128_t chr_device_info_uuid = BLE_UUID128_INIT(
    0x61, 0x5E, 0x4D, 0x3C, 0x0B, 0x2F, 0x8A, 0x9B,
    0x27, 0x4A, 0x1E, 0x3D, 0xD2, 0xB4, 0xC5, 0xF0
);

static const ble_uuid128_t chr_telemetry_uuid = BLE_UUID128_INIT(
    0x62, 0x5E, 0x4D, 0x3C, 0x0B, 0x2F, 0x8A, 0x9B,
    0x27, 0x4A, 0x1E, 0x3D, 0xD2, 0xB4, 0xC5, 0xF0
);

static const ble_uuid128_t chr_command_rx_uuid = BLE_UUID128_INIT(
    0x63, 0x5E, 0x4D, 0x3C, 0x0B, 0x2F, 0x8A, 0x9B,
    0x27, 0x4A, 0x1E, 0x3D, 0xD2, 0xB4, 0xC5, 0xF0
);

static const ble_uuid128_t chr_events_acks_uuid = BLE_UUID128_INIT(
    0x64, 0x5E, 0x4D, 0x3C, 0x0B, 0x2F, 0x8A, 0x9B,
    0x27, 0x4A, 0x1E, 0x3D, 0xD2, 0xB4, 0xC5, 0xF0
);

/* Device Info characteristic data */
static const uint8_t device_info_data[] = {
    WIRE_PROTO_VERSION,                     // proto_ver
    FW_VERSION_MAJOR,                       // fw_major
    FW_VERSION_MINOR,                       // fw_minor
    FW_VERSION_PATCH,                       // fw_patch
    (FW_BUILD_ID >> 0) & 0xFF,              // build_id (little-endian)
    (FW_BUILD_ID >> 8) & 0xFF,
    (FW_BUILD_ID >> 16) & 0xFF,
    (FW_BUILD_ID >> 24) & 0xFF,
    CAP_SUPPORTS_SESSION_LEASE & 0xFF,      // cap_bits (little-endian)
    0, 0, 0
};

/* Forward declarations */
static int gatt_chr_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                              struct ble_gatt_access_ctxt *ctxt, void *arg);
static void handle_command(uint16_t conn_handle, const uint8_t *data, size_t len);
static void send_ack(uint16_t acked_seq, uint16_t cmd_id, uint8_t status, uint16_t detail,
                     const uint8_t *opt_data, size_t opt_len);

/* GATT service definition */
static const struct ble_gatt_svc_def gatt_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                /* Device Info - Read */
                .uuid = &chr_device_info_uuid.u,
                .access_cb = gatt_chr_access_cb,
                .val_handle = &s_device_info_handle,
                .flags = BLE_GATT_CHR_F_READ,
            },
            {
                /* Telemetry Stream - Notify */
                .uuid = &chr_telemetry_uuid.u,
                .access_cb = gatt_chr_access_cb,
                .val_handle = &s_telemetry_handle,
                .flags = BLE_GATT_CHR_F_NOTIFY,
            },
            {
                /* Command RX - Write, Write Without Response */
                .uuid = &chr_command_rx_uuid.u,
                .access_cb = gatt_chr_access_cb,
                .val_handle = &s_command_rx_handle,
                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
            },
            {
                /* Events + Acks - Indicate, Notify */
                .uuid = &chr_events_acks_uuid.u,
                .access_cb = gatt_chr_access_cb,
                .val_handle = &s_events_acks_handle,
                .flags = BLE_GATT_CHR_F_NOTIFY | BLE_GATT_CHR_F_INDICATE,
            },
            { 0 } /* Terminator */
        },
    },
    { 0 } /* Terminator */
};

/* GATT characteristic access callback */
static int gatt_chr_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                              struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)arg;

    if (attr_handle == s_device_info_handle) {
        if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
            int rc = os_mbuf_append(ctxt->om, device_info_data, sizeof(device_info_data));
            return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
        }
    }
    else if (attr_handle == s_command_rx_handle) {
        if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
            uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
            uint8_t buf[WIRE_MAX_FRAME_SIZE];

            if (len > sizeof(buf)) {
                ESP_LOGW(TAG, "Command too large: %u bytes", len);
                return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
            }

            int rc = ble_hs_mbuf_to_flat(ctxt->om, buf, len, NULL);
            if (rc != 0) {
                return BLE_ATT_ERR_UNLIKELY;
            }

            handle_command(conn_handle, buf, len);
            return 0;
        }
    }

    return 0;
}

/* Handle incoming command */
static void handle_command(uint16_t conn_handle, const uint8_t *data, size_t len)
{
    ESP_LOGI(TAG, "Received command write: %u bytes", (unsigned)len);
    ESP_LOG_BUFFER_HEX_LEVEL(TAG, data, len, ESP_LOG_INFO);

    wire_frame_header_t header;
    const uint8_t *payload;

    if (!wire_parse_frame(data, len, &header, &payload)) {
        ESP_LOGW(TAG, "Invalid frame (len=%u)", (unsigned)len);
        ESP_LOG_BUFFER_HEX_LEVEL(TAG, data, len > 32 ? 32 : len, ESP_LOG_WARN);
        return;
    }

    if (header.msg_type != MSG_TYPE_COMMAND) {
        ESP_LOGW(TAG, "Unexpected msg_type: 0x%02X", header.msg_type);
        return;
    }

    if (header.payload_len < sizeof(wire_cmd_header_t)) {
        ESP_LOGW(TAG, "Command payload too short");
        return;
    }

    /* Parse command header */
    uint16_t cmd_id = payload[0] | ((uint16_t)payload[1] << 8);
    // uint16_t flags = payload[2] | ((uint16_t)payload[3] << 8);
    const uint8_t *cmd_payload = &payload[sizeof(wire_cmd_header_t)];
    size_t cmd_payload_len = header.payload_len - sizeof(wire_cmd_header_t);

    ESP_LOGI(TAG, "Command: cmd_id=0x%04X seq=%u payload_len=%u",
             cmd_id, header.seq, (unsigned)cmd_payload_len);

    /* Signal activity to reset lazy polling timer (but NOT for KEEPALIVE,
     * which is sent automatically and shouldn't prevent idle timeout) */
    if (cmd_id != CMD_KEEPALIVE) {
        pid_controller_signal_activity();
    }

    switch (cmd_id) {
        case CMD_OPEN_SESSION: {
            if (cmd_payload_len < sizeof(wire_cmd_open_session_t)) {
                send_ack(header.seq, cmd_id, CMD_STATUS_INVALID_ARGS, 0, NULL, 0);
                break;
            }

            uint32_t client_nonce = cmd_payload[0] |
                                    ((uint32_t)cmd_payload[1] << 8) |
                                    ((uint32_t)cmd_payload[2] << 16) |
                                    ((uint32_t)cmd_payload[3] << 24);

            uint32_t session_id;
            uint16_t lease_ms;
            esp_err_t err = session_mgr_open(client_nonce, &session_id, &lease_ms);

            if (err == ESP_OK) {
                /* Build ACK with session_id + lease_ms */
                uint8_t opt_data[6];
                opt_data[0] = session_id & 0xFF;
                opt_data[1] = (session_id >> 8) & 0xFF;
                opt_data[2] = (session_id >> 16) & 0xFF;
                opt_data[3] = (session_id >> 24) & 0xFF;
                opt_data[4] = lease_ms & 0xFF;
                opt_data[5] = (lease_ms >> 8) & 0xFF;

                send_ack(header.seq, cmd_id, CMD_STATUS_OK, 0, opt_data, sizeof(opt_data));
                ESP_LOGI(TAG, "OPEN_SESSION OK: session=0x%08lx lease=%ums",
                         (unsigned long)session_id, lease_ms);
            } else {
                send_ack(header.seq, cmd_id, CMD_STATUS_BUSY, 0, NULL, 0);
            }
            break;
        }

        case CMD_KEEPALIVE: {
            if (cmd_payload_len < sizeof(wire_cmd_keepalive_t)) {
                send_ack(header.seq, cmd_id, CMD_STATUS_INVALID_ARGS, 0, NULL, 0);
                break;
            }

            uint32_t session_id = cmd_payload[0] |
                                  ((uint32_t)cmd_payload[1] << 8) |
                                  ((uint32_t)cmd_payload[2] << 16) |
                                  ((uint32_t)cmd_payload[3] << 24);

            esp_err_t err = session_mgr_keepalive(session_id);

            if (err == ESP_OK) {
                send_ack(header.seq, cmd_id, CMD_STATUS_OK, 0, NULL, 0);
            } else {
                send_ack(header.seq, cmd_id, CMD_STATUS_REJECTED_POLICY, 0x0001, NULL, 0);
                ESP_LOGW(TAG, "KEEPALIVE rejected: invalid session");
            }
            break;
        }

        case CMD_START_RUN: {
            if (cmd_payload_len < 5) {
                send_ack(header.seq, cmd_id, CMD_STATUS_INVALID_ARGS, 0, NULL, 0);
                break;
            }

            uint32_t session_id = cmd_payload[0] |
                                  ((uint32_t)cmd_payload[1] << 8) |
                                  ((uint32_t)cmd_payload[2] << 16) |
                                  ((uint32_t)cmd_payload[3] << 24);
            uint8_t run_mode = cmd_payload[4];

            ESP_LOGI(TAG, "START_RUN: session=0x%08lx mode=%u",
                     (unsigned long)session_id, run_mode);

            /* Use machine state manager to handle the transition */
            esp_err_t err = machine_state_start_run(session_id, run_mode, 0, 0);
            if (err == ESP_OK) {
                send_ack(header.seq, cmd_id, CMD_STATUS_OK, 0, NULL, 0);
            } else if (err == ESP_ERR_INVALID_ARG) {
                send_ack(header.seq, cmd_id, CMD_STATUS_REJECTED_POLICY, 0x0001, NULL, 0);
                ESP_LOGW(TAG, "START_RUN rejected: invalid session");
            } else if (err == ESP_ERR_INVALID_STATE) {
                send_ack(header.seq, cmd_id, CMD_STATUS_NOT_READY, 0, NULL, 0);
                ESP_LOGW(TAG, "START_RUN rejected: not in IDLE state");
            } else if (err == ESP_ERR_NOT_ALLOWED) {
                uint8_t interlocks = machine_state_get_interlocks();
                send_ack(header.seq, cmd_id, CMD_STATUS_REJECTED_POLICY, 0x0002, &interlocks, 1);
                ESP_LOGW(TAG, "START_RUN rejected: interlocks=0x%02X", interlocks);
            } else {
                send_ack(header.seq, cmd_id, CMD_STATUS_HW_FAULT, 0, NULL, 0);
            }
            break;
        }

        case CMD_STOP_RUN: {
            if (cmd_payload_len < 5) {
                send_ack(header.seq, cmd_id, CMD_STATUS_INVALID_ARGS, 0, NULL, 0);
                break;
            }

            uint32_t session_id = cmd_payload[0] |
                                  ((uint32_t)cmd_payload[1] << 8) |
                                  ((uint32_t)cmd_payload[2] << 16) |
                                  ((uint32_t)cmd_payload[3] << 24);
            uint8_t stop_mode = cmd_payload[4];

            ESP_LOGI(TAG, "STOP_RUN: session=0x%08lx mode=%u",
                     (unsigned long)session_id, stop_mode);

            esp_err_t err = machine_state_stop_run(session_id, stop_mode);
            if (err == ESP_OK) {
                send_ack(header.seq, cmd_id, CMD_STATUS_OK, 0, NULL, 0);
            } else if (err == ESP_ERR_INVALID_ARG) {
                send_ack(header.seq, cmd_id, CMD_STATUS_REJECTED_POLICY, 0x0001, NULL, 0);
            } else {
                send_ack(header.seq, cmd_id, CMD_STATUS_NOT_READY, 0, NULL, 0);
            }
            break;
        }

        case CMD_ENABLE_SERVICE_MODE: {
            if (cmd_payload_len < 4) {
                send_ack(header.seq, cmd_id, CMD_STATUS_INVALID_ARGS, 0, NULL, 0);
                break;
            }

            uint32_t session_id = cmd_payload[0] |
                                  ((uint32_t)cmd_payload[1] << 8) |
                                  ((uint32_t)cmd_payload[2] << 16) |
                                  ((uint32_t)cmd_payload[3] << 24);

            ESP_LOGI(TAG, "ENABLE_SERVICE_MODE: session=0x%08lx", (unsigned long)session_id);

            esp_err_t err = machine_state_enter_service(session_id);
            if (err == ESP_OK) {
                send_ack(header.seq, cmd_id, CMD_STATUS_OK, 0, NULL, 0);
            } else if (err == ESP_ERR_INVALID_ARG) {
                send_ack(header.seq, cmd_id, CMD_STATUS_REJECTED_POLICY, 0x0001, NULL, 0);
            } else {
                send_ack(header.seq, cmd_id, CMD_STATUS_NOT_READY, 0, NULL, 0);
            }
            break;
        }

        case CMD_DISABLE_SERVICE_MODE: {
            if (cmd_payload_len < 4) {
                send_ack(header.seq, cmd_id, CMD_STATUS_INVALID_ARGS, 0, NULL, 0);
                break;
            }

            uint32_t session_id = cmd_payload[0] |
                                  ((uint32_t)cmd_payload[1] << 8) |
                                  ((uint32_t)cmd_payload[2] << 16) |
                                  ((uint32_t)cmd_payload[3] << 24);

            ESP_LOGI(TAG, "DISABLE_SERVICE_MODE: session=0x%08lx", (unsigned long)session_id);

            esp_err_t err = machine_state_exit_service(session_id);
            if (err == ESP_OK) {
                send_ack(header.seq, cmd_id, CMD_STATUS_OK, 0, NULL, 0);
            } else if (err == ESP_ERR_INVALID_ARG) {
                send_ack(header.seq, cmd_id, CMD_STATUS_REJECTED_POLICY, 0x0001, NULL, 0);
            } else {
                send_ack(header.seq, cmd_id, CMD_STATUS_NOT_READY, 0, NULL, 0);
            }
            break;
        }

        case CMD_CLEAR_ESTOP: {
            if (cmd_payload_len < 4) {
                send_ack(header.seq, cmd_id, CMD_STATUS_INVALID_ARGS, 0, NULL, 0);
                break;
            }

            uint32_t session_id = cmd_payload[0] |
                                  ((uint32_t)cmd_payload[1] << 8) |
                                  ((uint32_t)cmd_payload[2] << 16) |
                                  ((uint32_t)cmd_payload[3] << 24);

            ESP_LOGI(TAG, "CLEAR_ESTOP: session=0x%08lx", (unsigned long)session_id);

            esp_err_t err = machine_state_clear_estop(session_id);
            if (err == ESP_OK) {
                send_ack(header.seq, cmd_id, CMD_STATUS_OK, 0, NULL, 0);
            } else if (err == ESP_ERR_INVALID_ARG) {
                send_ack(header.seq, cmd_id, CMD_STATUS_REJECTED_POLICY, 0x0001, NULL, 0);
            } else {
                /* E-stop still active */
                send_ack(header.seq, cmd_id, CMD_STATUS_NOT_READY, 0x0003, NULL, 0);
            }
            break;
        }

        case CMD_CLEAR_LATCHED_ALARMS: {
            if (cmd_payload_len < 4) {
                send_ack(header.seq, cmd_id, CMD_STATUS_INVALID_ARGS, 0, NULL, 0);
                break;
            }

            uint32_t session_id = cmd_payload[0] |
                                  ((uint32_t)cmd_payload[1] << 8) |
                                  ((uint32_t)cmd_payload[2] << 16) |
                                  ((uint32_t)cmd_payload[3] << 24);

            ESP_LOGI(TAG, "CLEAR_LATCHED_ALARMS: session=0x%08lx", (unsigned long)session_id);

            esp_err_t err = machine_state_clear_fault(session_id);
            if (err == ESP_OK) {
                send_ack(header.seq, cmd_id, CMD_STATUS_OK, 0, NULL, 0);
            } else if (err == ESP_ERR_INVALID_ARG) {
                send_ack(header.seq, cmd_id, CMD_STATUS_REJECTED_POLICY, 0x0001, NULL, 0);
            } else {
                send_ack(header.seq, cmd_id, CMD_STATUS_NOT_READY, 0, NULL, 0);
            }
            break;
        }

        case CMD_SET_RELAY: {
            /* Payload: relay_index (u8), state (u8) */
            if (cmd_payload_len < 2) {
                ESP_LOGW(TAG, "SET_RELAY: payload too short (%u bytes)", (unsigned)cmd_payload_len);
                send_ack(header.seq, cmd_id, CMD_STATUS_INVALID_ARGS, 0, NULL, 0);
                break;
            }

            uint8_t relay_index = cmd_payload[0];
            uint8_t state = cmd_payload[1];

            ESP_LOGI(TAG, "SET_RELAY: relay_index=%u state=%u", relay_index, state);

            /* Validate relay_index is 1-8 */
            if (relay_index < 1 || relay_index > 8) {
                ESP_LOGW(TAG, "SET_RELAY: invalid relay_index %u (must be 1-8)", relay_index);
                send_ack(header.seq, cmd_id, CMD_STATUS_INVALID_ARGS, 0x0005, NULL, 0);
                break;
            }

            /* Validate state is 0, 1, or 2 */
            if (state > 2) {
                ESP_LOGW(TAG, "SET_RELAY: invalid state %u (must be 0=OFF, 1=ON, 2=TOGGLE)", state);
                send_ack(header.seq, cmd_id, CMD_STATUS_INVALID_ARGS, 0x0005, NULL, 0);
                break;
            }

            /* Control hardware relay via TCA9554 I/O expander */
            esp_err_t relay_err = relay_ctrl_set(relay_index, state);
            if (relay_err != ESP_OK) {
                ESP_LOGE(TAG, "SET_RELAY: hardware control failed: %s", esp_err_to_name(relay_err));
                send_ack(header.seq, cmd_id, CMD_STATUS_HW_FAULT, 0, NULL, 0);
                break;
            }

            /* Update telemetry to match hardware state */
            uint8_t ro_bits = relay_ctrl_get_state();
            telemetry_set_ro_bits(ro_bits);

            uint8_t bit_mask = (1 << (relay_index - 1));
            ESP_LOGI(TAG, "SET_RELAY OK: relay %u -> %s (ro_bits=0x%02X)",
                     relay_index,
                     (ro_bits & bit_mask) ? "ON" : "OFF",
                     ro_bits);

            send_ack(header.seq, cmd_id, CMD_STATUS_OK, 0, NULL, 0);
            break;
        }

        case CMD_SET_RELAY_MASK: {
            /* Payload: mask (u8), values (u8) */
            if (cmd_payload_len < 2) {
                ESP_LOGW(TAG, "SET_RELAY_MASK: payload too short (%u bytes)", (unsigned)cmd_payload_len);
                send_ack(header.seq, cmd_id, CMD_STATUS_INVALID_ARGS, 0, NULL, 0);
                break;
            }

            uint8_t mask = cmd_payload[0];
            uint8_t values = cmd_payload[1];

            ESP_LOGI(TAG, "SET_RELAY_MASK: mask=0x%02X values=0x%02X", mask, values);

            /* Validate mask is non-zero */
            if (mask == 0) {
                ESP_LOGW(TAG, "SET_RELAY_MASK: mask is zero (no channels affected)");
                send_ack(header.seq, cmd_id, CMD_STATUS_INVALID_ARGS, 0x0005, NULL, 0);
                break;
            }

            /* Get current state before change for logging */
            uint8_t old_ro_bits = relay_ctrl_get_state();

            /* Control hardware relays via TCA9554 I/O expander */
            esp_err_t relay_err = relay_ctrl_set_mask(mask, values);
            if (relay_err != ESP_OK) {
                ESP_LOGE(TAG, "SET_RELAY_MASK: hardware control failed: %s", esp_err_to_name(relay_err));
                send_ack(header.seq, cmd_id, CMD_STATUS_HW_FAULT, 0, NULL, 0);
                break;
            }

            /* Update telemetry to match hardware state */
            uint8_t new_ro_bits = relay_ctrl_get_state();
            telemetry_set_ro_bits(new_ro_bits);

            ESP_LOGI(TAG, "SET_RELAY_MASK OK: ro_bits 0x%02X -> 0x%02X (mask=0x%02X values=0x%02X)",
                     old_ro_bits, new_ro_bits, mask, values);

            send_ack(header.seq, cmd_id, CMD_STATUS_OK, 0, NULL, 0);
            break;
        }

        /* ===== PID Controller Commands ===== */

        case CMD_SET_SV: {
            /* Payload: controller_id (u8), sv_x10 (i16) */
            if (cmd_payload_len < sizeof(wire_cmd_set_sv_t)) {
                send_ack(header.seq, cmd_id, CMD_STATUS_INVALID_ARGS, 0, NULL, 0);
                break;
            }

            uint8_t ctrl_id = cmd_payload[0];
            int16_t sv_x10 = (int16_t)(cmd_payload[1] | ((uint16_t)cmd_payload[2] << 8));
            float sv_celsius = sv_x10 / 10.0f;

            ESP_LOGI(TAG, "SET_SV: controller=%u sv=%.1f C", ctrl_id, sv_celsius);

            if (ctrl_id < 1 || ctrl_id > PID_MAX_CONTROLLERS) {
                send_ack(header.seq, cmd_id, CMD_STATUS_INVALID_ARGS, 0x0005, NULL, 0);
                break;
            }

            esp_err_t err = pid_controller_set_sv(ctrl_id, sv_celsius);
            if (err == ESP_OK) {
                /* Force a poll to update cached data */
                pid_controller_force_poll(ctrl_id);
                send_ack(header.seq, cmd_id, CMD_STATUS_OK, 0, NULL, 0);
            } else if (err == ESP_ERR_INVALID_STATE) {
                send_ack(header.seq, cmd_id, CMD_STATUS_NOT_READY, 0, NULL, 0);
            } else {
                send_ack(header.seq, cmd_id, CMD_STATUS_TIMEOUT, 0x0004, NULL, 0);
            }
            break;
        }

        case CMD_SET_MODE: {
            /* Payload: controller_id (u8), mode (u8) */
            if (cmd_payload_len < sizeof(wire_cmd_set_mode_t)) {
                send_ack(header.seq, cmd_id, CMD_STATUS_INVALID_ARGS, 0, NULL, 0);
                break;
            }

            uint8_t ctrl_id = cmd_payload[0];
            uint8_t mode = cmd_payload[1];

            ESP_LOGI(TAG, "SET_MODE: controller=%u mode=%u", ctrl_id, mode);

            if (ctrl_id < 1 || ctrl_id > PID_MAX_CONTROLLERS) {
                send_ack(header.seq, cmd_id, CMD_STATUS_INVALID_ARGS, 0x0005, NULL, 0);
                break;
            }

            esp_err_t err = pid_controller_set_mode(ctrl_id, mode);
            if (err == ESP_OK) {
                pid_controller_force_poll(ctrl_id);
                send_ack(header.seq, cmd_id, CMD_STATUS_OK, 0, NULL, 0);
            } else if (err == ESP_ERR_INVALID_ARG) {
                send_ack(header.seq, cmd_id, CMD_STATUS_INVALID_ARGS, 0x0005, NULL, 0);
            } else if (err == ESP_ERR_INVALID_STATE) {
                send_ack(header.seq, cmd_id, CMD_STATUS_NOT_READY, 0, NULL, 0);
            } else {
                send_ack(header.seq, cmd_id, CMD_STATUS_TIMEOUT, 0x0004, NULL, 0);
            }
            break;
        }

        case CMD_REQUEST_PV_SV_REFRESH: {
            /* Payload: controller_id (u8) */
            if (cmd_payload_len < 1) {
                send_ack(header.seq, cmd_id, CMD_STATUS_INVALID_ARGS, 0, NULL, 0);
                break;
            }

            uint8_t ctrl_id = cmd_payload[0];

            ESP_LOGI(TAG, "REQUEST_PV_SV_REFRESH: controller=%u", ctrl_id);

            if (ctrl_id < 1 || ctrl_id > PID_MAX_CONTROLLERS) {
                send_ack(header.seq, cmd_id, CMD_STATUS_INVALID_ARGS, 0x0005, NULL, 0);
                break;
            }

            esp_err_t err = pid_controller_force_poll(ctrl_id);
            if (err == ESP_OK) {
                send_ack(header.seq, cmd_id, CMD_STATUS_OK, 0, NULL, 0);
            } else if (err == ESP_ERR_NOT_FOUND) {
                send_ack(header.seq, cmd_id, CMD_STATUS_INVALID_ARGS, 0x0005, NULL, 0);
            } else {
                send_ack(header.seq, cmd_id, CMD_STATUS_NOT_READY, 0, NULL, 0);
            }
            break;
        }

        case CMD_SET_PID_PARAMS: {
            /* Payload: controller_id (u8), p_gain_x10 (i16), i_time (u16), d_time (u16) */
            if (cmd_payload_len < sizeof(wire_cmd_set_pid_params_t)) {
                send_ack(header.seq, cmd_id, CMD_STATUS_INVALID_ARGS, 0, NULL, 0);
                break;
            }

            uint8_t ctrl_id = cmd_payload[0];
            int16_t p_x10 = (int16_t)(cmd_payload[1] | ((uint16_t)cmd_payload[2] << 8));
            uint16_t i_time = cmd_payload[3] | ((uint16_t)cmd_payload[4] << 8);
            uint16_t d_time = cmd_payload[5] | ((uint16_t)cmd_payload[6] << 8);
            float p_gain = p_x10 / 10.0f;

            ESP_LOGI(TAG, "SET_PID_PARAMS: controller=%u P=%.1f I=%u D=%u",
                     ctrl_id, p_gain, i_time, d_time);

            if (ctrl_id < 1 || ctrl_id > PID_MAX_CONTROLLERS) {
                send_ack(header.seq, cmd_id, CMD_STATUS_INVALID_ARGS, 0x0005, NULL, 0);
                break;
            }

            esp_err_t err = pid_controller_write_params(ctrl_id, p_gain, i_time, d_time);
            if (err == ESP_OK) {
                send_ack(header.seq, cmd_id, CMD_STATUS_OK, 0, NULL, 0);
            } else if (err == ESP_ERR_INVALID_STATE) {
                send_ack(header.seq, cmd_id, CMD_STATUS_NOT_READY, 0, NULL, 0);
            } else {
                send_ack(header.seq, cmd_id, CMD_STATUS_TIMEOUT, 0x0004, NULL, 0);
            }
            break;
        }

        case CMD_READ_PID_PARAMS: {
            /* Payload: controller_id (u8) */
            if (cmd_payload_len < 1) {
                send_ack(header.seq, cmd_id, CMD_STATUS_INVALID_ARGS, 0, NULL, 0);
                break;
            }

            uint8_t ctrl_id = cmd_payload[0];

            ESP_LOGI(TAG, "READ_PID_PARAMS: controller=%u", ctrl_id);

            if (ctrl_id < 1 || ctrl_id > PID_MAX_CONTROLLERS) {
                send_ack(header.seq, cmd_id, CMD_STATUS_INVALID_ARGS, 0x0005, NULL, 0);
                break;
            }

            float p_gain;
            uint16_t i_time, d_time;
            esp_err_t err = pid_controller_read_params(ctrl_id, &p_gain, &i_time, &d_time);

            if (err == ESP_OK) {
                wire_ack_pid_params_t params;
                params.controller_id = ctrl_id;
                params.p_gain_x10 = (int16_t)(p_gain * 10.0f + 0.5f);
                params.i_time = i_time;
                params.d_time = d_time;
                send_ack(header.seq, cmd_id, CMD_STATUS_OK, 0,
                         (const uint8_t *)&params, sizeof(params));
            } else if (err == ESP_ERR_INVALID_STATE) {
                send_ack(header.seq, cmd_id, CMD_STATUS_NOT_READY, 0, NULL, 0);
            } else {
                send_ack(header.seq, cmd_id, CMD_STATUS_TIMEOUT, 0x0004, NULL, 0);
            }
            break;
        }

        case CMD_START_AUTOTUNE: {
            /* Payload: controller_id (u8) */
            if (cmd_payload_len < sizeof(wire_cmd_autotune_t)) {
                send_ack(header.seq, cmd_id, CMD_STATUS_INVALID_ARGS, 0, NULL, 0);
                break;
            }

            uint8_t ctrl_id = cmd_payload[0];

            ESP_LOGI(TAG, "START_AUTOTUNE: controller=%u", ctrl_id);

            if (ctrl_id < 1 || ctrl_id > PID_MAX_CONTROLLERS) {
                send_ack(header.seq, cmd_id, CMD_STATUS_INVALID_ARGS, 0x0005, NULL, 0);
                break;
            }

            esp_err_t err = pid_controller_start_autotune(ctrl_id);
            if (err == ESP_OK) {
                send_ack(header.seq, cmd_id, CMD_STATUS_OK, 0, NULL, 0);
            } else if (err == ESP_ERR_INVALID_STATE) {
                send_ack(header.seq, cmd_id, CMD_STATUS_NOT_READY, 0, NULL, 0);
            } else {
                send_ack(header.seq, cmd_id, CMD_STATUS_TIMEOUT, 0x0004, NULL, 0);
            }
            break;
        }

        case CMD_STOP_AUTOTUNE: {
            /* Payload: controller_id (u8) */
            if (cmd_payload_len < sizeof(wire_cmd_autotune_t)) {
                send_ack(header.seq, cmd_id, CMD_STATUS_INVALID_ARGS, 0, NULL, 0);
                break;
            }

            uint8_t ctrl_id = cmd_payload[0];

            ESP_LOGI(TAG, "STOP_AUTOTUNE: controller=%u", ctrl_id);

            if (ctrl_id < 1 || ctrl_id > PID_MAX_CONTROLLERS) {
                send_ack(header.seq, cmd_id, CMD_STATUS_INVALID_ARGS, 0x0005, NULL, 0);
                break;
            }

            esp_err_t err = pid_controller_stop_autotune(ctrl_id);
            if (err == ESP_OK) {
                send_ack(header.seq, cmd_id, CMD_STATUS_OK, 0, NULL, 0);
            } else if (err == ESP_ERR_INVALID_STATE) {
                send_ack(header.seq, cmd_id, CMD_STATUS_NOT_READY, 0, NULL, 0);
            } else {
                send_ack(header.seq, cmd_id, CMD_STATUS_TIMEOUT, 0x0004, NULL, 0);
            }
            break;
        }

        case CMD_SET_ALARM_LIMITS: {
            /* Payload: controller_id (u8), alarm1_x10 (i16), alarm2_x10 (i16) */
            if (cmd_payload_len < sizeof(wire_cmd_set_alarm_limits_t)) {
                send_ack(header.seq, cmd_id, CMD_STATUS_INVALID_ARGS, 0, NULL, 0);
                break;
            }

            uint8_t ctrl_id = cmd_payload[0];
            int16_t al1_x10 = (int16_t)(cmd_payload[1] | ((uint16_t)cmd_payload[2] << 8));
            int16_t al2_x10 = (int16_t)(cmd_payload[3] | ((uint16_t)cmd_payload[4] << 8));
            float al1 = al1_x10 / 10.0f;
            float al2 = al2_x10 / 10.0f;

            ESP_LOGI(TAG, "SET_ALARM_LIMITS: controller=%u AL1=%.1f AL2=%.1f",
                     ctrl_id, al1, al2);

            if (ctrl_id < 1 || ctrl_id > PID_MAX_CONTROLLERS) {
                send_ack(header.seq, cmd_id, CMD_STATUS_INVALID_ARGS, 0x0005, NULL, 0);
                break;
            }

            esp_err_t err = pid_controller_set_alarm_limits(ctrl_id, al1, al2);
            if (err == ESP_OK) {
                send_ack(header.seq, cmd_id, CMD_STATUS_OK, 0, NULL, 0);
            } else if (err == ESP_ERR_INVALID_STATE) {
                send_ack(header.seq, cmd_id, CMD_STATUS_NOT_READY, 0, NULL, 0);
            } else {
                send_ack(header.seq, cmd_id, CMD_STATUS_TIMEOUT, 0x0004, NULL, 0);
            }
            break;
        }

        case CMD_READ_ALARM_LIMITS: {
            /* Payload: controller_id (u8) */
            if (cmd_payload_len < 1) {
                send_ack(header.seq, cmd_id, CMD_STATUS_INVALID_ARGS, 0, NULL, 0);
                break;
            }

            uint8_t ctrl_id = cmd_payload[0];

            ESP_LOGI(TAG, "READ_ALARM_LIMITS: controller=%u", ctrl_id);

            if (ctrl_id < 1 || ctrl_id > PID_MAX_CONTROLLERS) {
                send_ack(header.seq, cmd_id, CMD_STATUS_INVALID_ARGS, 0x0005, NULL, 0);
                break;
            }

            float al1, al2;
            esp_err_t err = pid_controller_read_alarm_limits(ctrl_id, &al1, &al2);

            if (err == ESP_OK) {
                wire_ack_alarm_limits_t limits;
                limits.controller_id = ctrl_id;
                limits.alarm1_x10 = (int16_t)(al1 * 10.0f + 0.5f);
                limits.alarm2_x10 = (int16_t)(al2 * 10.0f + 0.5f);
                send_ack(header.seq, cmd_id, CMD_STATUS_OK, 0,
                         (const uint8_t *)&limits, sizeof(limits));
            } else if (err == ESP_ERR_INVALID_STATE) {
                send_ack(header.seq, cmd_id, CMD_STATUS_NOT_READY, 0, NULL, 0);
            } else {
                send_ack(header.seq, cmd_id, CMD_STATUS_TIMEOUT, 0x0004, NULL, 0);
            }
            break;
        }

        /* ===== Generic Register Commands ===== */

        case CMD_READ_REGISTERS: {
            /* Payload: controller_id (u8), start_address (u16 LE), count (u8) */
            if (cmd_payload_len < sizeof(wire_cmd_read_registers_t)) {
                send_ack(header.seq, cmd_id, CMD_STATUS_INVALID_ARGS, 0, NULL, 0);
                break;
            }

            uint8_t ctrl_id = cmd_payload[0];
            uint16_t start_addr = cmd_payload[1] | ((uint16_t)cmd_payload[2] << 8);
            uint8_t count = cmd_payload[3];

            ESP_LOGI(TAG, "READ_REGISTERS: controller=%u start=%u count=%u",
                     ctrl_id, start_addr, count);

            /* Validate controller_id */
            if (ctrl_id < 1 || ctrl_id > PID_MAX_CONTROLLERS) {
                send_ack(header.seq, cmd_id, CMD_STATUS_INVALID_ARGS, 0x0005, NULL, 0);
                break;
            }

            /* Validate count (1-16) */
            if (count == 0 || count > 16) {
                send_ack(header.seq, cmd_id, CMD_STATUS_INVALID_ARGS, 0x0005, NULL, 0);
                break;
            }

            /* Read registers */
            uint16_t values[16];
            esp_err_t err = pid_controller_read_registers(ctrl_id, start_addr, count, values);

            if (err == ESP_OK) {
                /* Build response: header (4 bytes) + values (2*count bytes) */
                uint8_t ack_data[4 + 32]; /* max 4 + 16*2 = 36 bytes */
                ack_data[0] = ctrl_id;
                ack_data[1] = start_addr & 0xFF;
                ack_data[2] = (start_addr >> 8) & 0xFF;
                ack_data[3] = count;
                for (int i = 0; i < count; i++) {
                    ack_data[4 + i*2] = values[i] & 0xFF;
                    ack_data[4 + i*2 + 1] = (values[i] >> 8) & 0xFF;
                }
                send_ack(header.seq, cmd_id, CMD_STATUS_OK, 0,
                         ack_data, 4 + count * 2);
            } else if (err == ESP_ERR_INVALID_STATE) {
                send_ack(header.seq, cmd_id, CMD_STATUS_NOT_READY, 0, NULL, 0);
            } else {
                send_ack(header.seq, cmd_id, CMD_STATUS_TIMEOUT, 0x0004, NULL, 0);
            }
            break;
        }

        case CMD_WRITE_REGISTER: {
            /* Payload: controller_id (u8), address (u16 LE), value (u16 LE) */
            if (cmd_payload_len < sizeof(wire_cmd_write_register_t)) {
                send_ack(header.seq, cmd_id, CMD_STATUS_INVALID_ARGS, 0, NULL, 0);
                break;
            }

            uint8_t ctrl_id = cmd_payload[0];
            uint16_t address = cmd_payload[1] | ((uint16_t)cmd_payload[2] << 8);
            uint16_t value = cmd_payload[3] | ((uint16_t)cmd_payload[4] << 8);

            ESP_LOGI(TAG, "WRITE_REGISTER: controller=%u addr=%u value=0x%04X",
                     ctrl_id, address, value);

            /* Validate controller_id */
            if (ctrl_id < 1 || ctrl_id > PID_MAX_CONTROLLERS) {
                send_ack(header.seq, cmd_id, CMD_STATUS_INVALID_ARGS, 0x0005, NULL, 0);
                break;
            }

            /* Protect RS-485 communication registers from writes */
            if (address >= 49 && address <= 51) {
                ESP_LOGW(TAG, "WRITE_REGISTER: protected register %u", address);
                send_ack(header.seq, cmd_id, CMD_STATUS_INVALID_ARGS, 0x0005, NULL, 0);
                break;
            }

            /* Write register with verification */
            uint16_t verified_value = 0;
            esp_err_t err = pid_controller_write_register(ctrl_id, address, value, &verified_value);

            if (err == ESP_OK) {
                /* Build response with verified value */
                wire_ack_write_register_t ack;
                ack.controller_id = ctrl_id;
                ack.address = address;
                ack.value = verified_value;
                send_ack(header.seq, cmd_id, CMD_STATUS_OK, 0,
                         (const uint8_t *)&ack, sizeof(ack));
            } else if (err == ESP_ERR_INVALID_STATE) {
                send_ack(header.seq, cmd_id, CMD_STATUS_NOT_READY, 0, NULL, 0);
            } else if (err == ESP_ERR_INVALID_RESPONSE) {
                /* Write succeeded but verification failed - return HW_FAULT with the actual value */
                wire_ack_write_register_t ack;
                ack.controller_id = ctrl_id;
                ack.address = address;
                ack.value = verified_value;
                send_ack(header.seq, cmd_id, CMD_STATUS_HW_FAULT, 0,
                         (const uint8_t *)&ack, sizeof(ack));
            } else {
                send_ack(header.seq, cmd_id, CMD_STATUS_TIMEOUT, 0x0004, NULL, 0);
            }
            break;
        }

        /* ===== Configuration Commands ===== */

        case CMD_SET_IDLE_TIMEOUT: {
            /* Payload: timeout_minutes (u8) */
            if (cmd_payload_len < 1) {
                send_ack(header.seq, cmd_id, CMD_STATUS_INVALID_ARGS, 0, NULL, 0);
                break;
            }

            uint8_t timeout_minutes = cmd_payload[0];

            ESP_LOGI(TAG, "SET_IDLE_TIMEOUT: %u minutes", timeout_minutes);

            esp_err_t err = pid_controller_set_idle_timeout(timeout_minutes);
            if (err == ESP_OK) {
                send_ack(header.seq, cmd_id, CMD_STATUS_OK, 0, NULL, 0);
            } else {
                send_ack(header.seq, cmd_id, CMD_STATUS_HW_FAULT, 0, NULL, 0);
            }
            break;
        }

        case CMD_GET_IDLE_TIMEOUT: {
            /* No payload required */
            ESP_LOGI(TAG, "GET_IDLE_TIMEOUT");

            uint8_t timeout_minutes = pid_controller_get_idle_timeout();
            send_ack(header.seq, cmd_id, CMD_STATUS_OK, 0, &timeout_minutes, 1);
            break;
        }

        default:
            ESP_LOGW(TAG, "Unknown command: 0x%04X", cmd_id);
            send_ack(header.seq, cmd_id, CMD_STATUS_INVALID_ARGS, 0, NULL, 0);
            break;
    }
}

/* Send command ACK */
static void send_ack(uint16_t acked_seq, uint16_t cmd_id, uint8_t status, uint16_t detail,
                     const uint8_t *opt_data, size_t opt_len)
{
    if (s_conn_handle == BLE_HS_CONN_HANDLE_NONE) {
        ESP_LOGW(TAG, "send_ack: no connection");
        return;
    }

    uint8_t frame[WIRE_MAX_FRAME_SIZE];
    size_t frame_len = wire_build_cmd_ack(frame, sizeof(frame), s_tx_seq++,
                                          acked_seq, cmd_id, status, detail,
                                          opt_data, opt_len);

    if (frame_len == 0) {
        ESP_LOGE(TAG, "Failed to build ACK frame");
        return;
    }

    /* Log the frame being sent */
    ESP_LOGI(TAG, "Sending ACK: acked_seq=%u cmd_id=0x%04X status=%u len=%u",
             acked_seq, cmd_id, status, (unsigned)frame_len);
    ESP_LOG_BUFFER_HEX_LEVEL(TAG, frame, frame_len, ESP_LOG_DEBUG);

    /* Check subscription status */
    bool can_indicate = s_events_indicate_subscribed;
    bool can_notify = s_events_notify_subscribed;

    if (!can_indicate && !can_notify) {
        ESP_LOGW(TAG, "Events+Acks not subscribed (indicate=%d notify=%d), sending anyway",
                 can_indicate, can_notify);
        /* Try anyway - some BLE stacks allow unsolicited notifications */
    }

    struct os_mbuf *om = ble_hs_mbuf_from_flat(frame, frame_len);
    if (!om) {
        ESP_LOGE(TAG, "Failed to allocate mbuf for ACK");
        return;
    }

    /* Prefer indication for critical commands, but fall back to notification */
    bool want_indicate = (cmd_id == CMD_OPEN_SESSION ||
                          cmd_id == CMD_START_RUN ||
                          cmd_id == CMD_STOP_RUN);

    int rc;
    if (want_indicate && can_indicate) {
        rc = ble_gatts_indicate_custom(s_conn_handle, s_events_acks_handle, om);
        ESP_LOGI(TAG, "Sent ACK via indication: rc=%d", rc);
    } else {
        /* Use notification (works even without explicit subscription on some stacks) */
        rc = ble_gatts_notify_custom(s_conn_handle, s_events_acks_handle, om);
        ESP_LOGI(TAG, "Sent ACK via notification: rc=%d", rc);
    }

    if (rc != 0) {
        ESP_LOGW(TAG, "Failed to send ACK: rc=%d (0=OK, 6=not subscribed, 14=no resources)", rc);
    }
}

/* GAP event handler */
static int gap_event_cb(struct ble_gap_event *event, void *arg)
{
    (void)arg;

    switch (event->type) {
        case BLE_GAP_EVENT_CONNECT:
            if (event->connect.status == 0) {
                s_conn_handle = event->connect.conn_handle;
                ESP_LOGI(TAG, "Client connected: conn_handle=%u", s_conn_handle);
                /* Update LED to show connected state */
                status_led_set_state(LED_STATE_CONNECTED_HEALTHY);
            } else {
                ESP_LOGW(TAG, "Connection failed: status=%d", event->connect.status);
                s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
            }
            break;

        case BLE_GAP_EVENT_DISCONNECT:
            ESP_LOGI(TAG, "Client disconnected: reason=%d", event->disconnect.reason);
            s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
            s_telemetry_subscribed = false;
            s_events_notify_subscribed = false;
            s_events_indicate_subscribed = false;

            /* Force-expire session on disconnect */
            session_mgr_force_expire();

            /* Brief disconnect indication then back to advertising */
            status_led_set_state(LED_STATE_ERROR_DISCONNECT);

            /* Restart advertising */
            ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER,
                              &(struct ble_gap_adv_params){
                                  .conn_mode = BLE_GAP_CONN_MODE_UND,
                                  .disc_mode = BLE_GAP_DISC_MODE_GEN,
                              },
                              gap_event_cb, NULL);
            break;

        case BLE_GAP_EVENT_SUBSCRIBE:
            ESP_LOGI(TAG, "Subscribe event: attr_handle=%u notify=%d indicate=%d",
                     event->subscribe.attr_handle,
                     event->subscribe.cur_notify,
                     event->subscribe.cur_indicate);

            if (event->subscribe.attr_handle == s_telemetry_handle) {
                s_telemetry_subscribed = event->subscribe.cur_notify;
                ESP_LOGI(TAG, "Telemetry subscription: %s",
                         s_telemetry_subscribed ? "enabled" : "disabled");
            } else if (event->subscribe.attr_handle == s_events_acks_handle) {
                s_events_notify_subscribed = event->subscribe.cur_notify;
                s_events_indicate_subscribed = event->subscribe.cur_indicate;
                ESP_LOGI(TAG, "Events/Acks subscription: notify=%d indicate=%d",
                         s_events_notify_subscribed, s_events_indicate_subscribed);
            }
            break;

        case BLE_GAP_EVENT_MTU:
            ESP_LOGI(TAG, "MTU update: %u", event->mtu.value);
            break;

        default:
            break;
    }

    return 0;
}

/* BLE host task */
static void ble_host_task(void *param)
{
    (void)param;
    ESP_LOGI(TAG, "BLE host task started");
    nimble_port_run();
    nimble_port_freertos_deinit();
}

/* Called when BLE host and controller are synced */
static void ble_on_sync(void)
{
    ESP_LOGI(TAG, "BLE sync complete");

    /* Set device address */
    int rc = ble_hs_util_ensure_addr(0);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to ensure address: rc=%d", rc);
        return;
    }

    /* Get MAC for device name suffix */
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_BT);
    snprintf(s_device_name, sizeof(s_device_name), "%s%02X%02X",
             BLE_DEVICE_NAME_PREFIX, mac[4], mac[5]);

    /* Set GAP device name */
    rc = ble_svc_gap_device_name_set(s_device_name);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to set device name: rc=%d", rc);
    }

    /* Build advertising data - keep it small (31 byte limit) */
    struct ble_hs_adv_fields adv_fields = {0};
    adv_fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    adv_fields.name = (uint8_t *)s_device_name;
    adv_fields.name_len = strlen(s_device_name);
    adv_fields.name_is_complete = 1;

    rc = ble_gap_adv_set_fields(&adv_fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to set adv fields: rc=%d", rc);
        return;
    }

    /* Put service UUID in scan response data (separate 31-byte budget) */
    struct ble_hs_adv_fields rsp_fields = {0};
    rsp_fields.uuids128 = (ble_uuid128_t *)&svc_uuid;
    rsp_fields.num_uuids128 = 1;
    rsp_fields.uuids128_is_complete = 1;

    rc = ble_gap_adv_rsp_set_fields(&rsp_fields);
    if (rc != 0) {
        ESP_LOGW(TAG, "Failed to set scan response fields: rc=%d", rc);
        /* Continue anyway - name advertising still works */
    }

    /* Start advertising */
    struct ble_gap_adv_params adv_params = {
        .conn_mode = BLE_GAP_CONN_MODE_UND,
        .disc_mode = BLE_GAP_DISC_MODE_GEN,
    };

    rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER,
                           &adv_params, gap_event_cb, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to start advertising: rc=%d", rc);
        return;
    }

    ESP_LOGI(TAG, "Advertising as '%s'", s_device_name);
}

/* Called on BLE host reset */
static void ble_on_reset(int reason)
{
    ESP_LOGW(TAG, "BLE host reset: reason=%d", reason);
}

esp_err_t ble_gatt_init(void)
{
    ESP_LOGI(TAG, "Initializing BLE GATT server");

    /* Initialize session manager */
    session_mgr_init();

    /* Initialize NimBLE */
    int rc = nimble_port_init();
    if (rc != ESP_OK) {
        ESP_LOGE(TAG, "nimble_port_init failed: rc=%d", rc);
        return ESP_FAIL;
    }

    /* Configure host callbacks */
    ble_hs_cfg.sync_cb = ble_on_sync;
    ble_hs_cfg.reset_cb = ble_on_reset;

    /* Initialize GAP and GATT services */
    ble_svc_gap_init();
    ble_svc_gatt_init();

    /* Register custom GATT service */
    rc = ble_gatts_count_cfg(gatt_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gatts_count_cfg failed: rc=%d", rc);
        return ESP_FAIL;
    }

    rc = ble_gatts_add_svcs(gatt_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gatts_add_svcs failed: rc=%d", rc);
        return ESP_FAIL;
    }

    /* Start BLE host task */
    nimble_port_freertos_init(ble_host_task);

    ESP_LOGI(TAG, "BLE GATT server initialized");
    return ESP_OK;
}

esp_err_t ble_gatt_send_telemetry(const uint8_t *data, size_t len)
{
    if (s_conn_handle == BLE_HS_CONN_HANDLE_NONE || !s_telemetry_subscribed) {
        return ESP_ERR_INVALID_STATE;
    }

    struct os_mbuf *om = ble_hs_mbuf_from_flat(data, len);
    if (!om) {
        return ESP_ERR_NO_MEM;
    }

    int rc = ble_gatts_notify_custom(s_conn_handle, s_telemetry_handle, om);
    if (rc != 0) {
        ESP_LOGW(TAG, "Failed to send telemetry: rc=%d", rc);
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t ble_gatt_send_event(const uint8_t *data, size_t len, bool indicate)
{
    if (s_conn_handle == BLE_HS_CONN_HANDLE_NONE ||
        (!s_events_notify_subscribed && !s_events_indicate_subscribed)) {
        return ESP_ERR_INVALID_STATE;
    }

    struct os_mbuf *om = ble_hs_mbuf_from_flat(data, len);
    if (!om) {
        return ESP_ERR_NO_MEM;
    }

    int rc;
    // Prefer indication if subscribed and requested, otherwise use notification
    if (indicate && s_events_indicate_subscribed) {
        rc = ble_gatts_indicate_custom(s_conn_handle, s_events_acks_handle, om);
    } else if (s_events_notify_subscribed) {
        rc = ble_gatts_notify_custom(s_conn_handle, s_events_acks_handle, om);
    } else {
        // Only indicate subscribed, use that
        rc = ble_gatts_indicate_custom(s_conn_handle, s_events_acks_handle, om);
    }

    if (rc != 0) {
        ESP_LOGW(TAG, "Failed to send event: rc=%d", rc);
        return ESP_FAIL;
    }

    return ESP_OK;
}

bool ble_gatt_is_connected(void)
{
    return s_conn_handle != BLE_HS_CONN_HANDLE_NONE;
}

bool ble_gatt_telemetry_subscribed(void)
{
    return s_telemetry_subscribed;
}

uint16_t ble_gatt_get_conn_handle(void)
{
    return s_conn_handle;
}
