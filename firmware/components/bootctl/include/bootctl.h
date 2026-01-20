#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize boot control subsystem
 *
 * Creates the boot control task and boot button monitor task.
 * Also marks the app as valid if rollback is pending.
 *
 * @return ESP_OK on success
 */
esp_err_t bootctl_init(void);

/**
 * @brief Store a partition label in NVS for "return to" functionality
 *
 * Used by recovery to know which partition to return to.
 *
 * @param label Partition label (e.g., "ota_0")
 * @return ESP_OK on success
 */
esp_err_t bootctl_store_return_label(const char *label);

/**
 * @brief Get the stored return label from NVS
 *
 * @param out_label Buffer to store the label
 * @param out_len Size of the buffer
 * @return ESP_OK if found, ESP_ERR_NOT_FOUND if not set
 */
esp_err_t bootctl_get_return_label(char *out_label, size_t out_len);

/**
 * @brief Trigger a switch to the factory/recovery partition
 *
 * Stores current partition as return label, sets boot to factory, and reboots.
 */
void bootctl_switch_to_recovery(void);

/**
 * @brief Mark the current app as valid (cancel rollback)
 *
 * Call this after your app has passed health checks.
 *
 * @return ESP_OK on success
 */
esp_err_t bootctl_mark_app_valid(void);

#ifdef __cplusplus
}
#endif
