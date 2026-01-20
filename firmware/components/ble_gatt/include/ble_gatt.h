#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * BLE GATT Server for System Control Service
 * ==========================================
 *
 * Service UUID: F0C5B4D2-3D1E-4A27-9B8A-2F0B3C4D5E60
 *
 * Characteristics:
 * - Device Info      (5E61): Read
 * - Telemetry Stream (5E62): Notify
 * - Command RX       (5E63): Write, Write Without Response
 * - Events + Acks    (5E64): Indicate, Notify
 * - Bulk Gateway     (5E65): Write, Notify (future)
 * - Diagnostic Log   (5E66): Notify (optional)
 */

/* GATT UUIDs - all share same base, differ in last byte */
#define BLE_SVC_UUID_BASE       "F0C5B4D2-3D1E-4A27-9B8A-2F0B3C4D5E60"
#define BLE_CHR_DEVICE_INFO     "F0C5B4D2-3D1E-4A27-9B8A-2F0B3C4D5E61"
#define BLE_CHR_TELEMETRY       "F0C5B4D2-3D1E-4A27-9B8A-2F0B3C4D5E62"
#define BLE_CHR_COMMAND_RX      "F0C5B4D2-3D1E-4A27-9B8A-2F0B3C4D5E63"
#define BLE_CHR_EVENTS_ACKS     "F0C5B4D2-3D1E-4A27-9B8A-2F0B3C4D5E64"
#define BLE_CHR_BULK_GATEWAY    "F0C5B4D2-3D1E-4A27-9B8A-2F0B3C4D5E65"
#define BLE_CHR_DIAG_LOG        "F0C5B4D2-3D1E-4A27-9B8A-2F0B3C4D5E66"

/* Device advertising name prefix */
#define BLE_DEVICE_NAME_PREFIX  "SYS-CTRL-"

/* Firmware version info for Device Info characteristic */
#define FW_VERSION_MAJOR        0
#define FW_VERSION_MINOR        1
#define FW_VERSION_PATCH        0
#define FW_BUILD_ID             0x00000001

/* Capability flags for Device Info */
#define CAP_SUPPORTS_SESSION_LEASE  (1 << 0)
#define CAP_SUPPORTS_EVENT_LOG      (1 << 1)
#define CAP_SUPPORTS_BULK_GATEWAY   (1 << 2)
#define CAP_SUPPORTS_MODBUS_TOOLS   (1 << 3)
#define CAP_SUPPORTS_PID_TUNING     (1 << 4)
#define CAP_SUPPORTS_OTA            (1 << 5)

/**
 * @brief Initialize and start the BLE GATT server
 *
 * Sets up NimBLE, registers the GATT service, and starts advertising.
 *
 * @return ESP_OK on success
 */
esp_err_t ble_gatt_init(void);

/**
 * @brief Send a telemetry notification to connected client
 *
 * @param data Telemetry frame data (already formatted with wire protocol)
 * @param len Length of the data
 * @return ESP_OK if sent, ESP_ERR_INVALID_STATE if not connected/subscribed
 */
esp_err_t ble_gatt_send_telemetry(const uint8_t *data, size_t len);

/**
 * @brief Send an event/ack notification to connected client
 *
 * @param data Event/ACK frame data
 * @param len Length of the data
 * @param indicate true for indication (reliable), false for notification
 * @return ESP_OK if sent
 */
esp_err_t ble_gatt_send_event(const uint8_t *data, size_t len, bool indicate);

/**
 * @brief Check if a client is connected
 *
 * @return true if connected
 */
bool ble_gatt_is_connected(void);

/**
 * @brief Check if telemetry notifications are enabled
 *
 * @return true if client has subscribed to telemetry
 */
bool ble_gatt_telemetry_subscribed(void);

/**
 * @brief Get the connection handle (for advanced use)
 *
 * @return Connection handle, or BLE_HS_CONN_HANDLE_NONE if not connected
 */
uint16_t ble_gatt_get_conn_handle(void);

#ifdef __cplusplus
}
#endif
