#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Session Manager
 * ===============
 * Manages HMI session lifecycle with lease-based heartbeat.
 *
 * Flow:
 * 1. App sends OPEN_SESSION with client_nonce
 * 2. ESP responds with session_id + lease_ms
 * 3. App sends KEEPALIVE every ~1 second to refresh lease
 * 4. If lease expires, session becomes stale (HMI_NOT_LIVE alarm)
 */

#define SESSION_DEFAULT_LEASE_MS    3000    // Default lease duration
#define SESSION_GRACE_PERIOD_MS     500     // Grace period before declaring stale

typedef enum {
    SESSION_STATE_NONE,         // No active session
    SESSION_STATE_LIVE,         // Session active and recently refreshed
    SESSION_STATE_STALE,        // Lease expired, but session still exists
} session_state_t;

typedef struct {
    uint32_t        session_id;
    uint32_t        client_nonce;
    uint16_t        lease_ms;
    int64_t         last_keepalive_us;
    session_state_t state;
} session_info_t;

/**
 * @brief Initialize the session manager
 */
esp_err_t session_mgr_init(void);

/**
 * @brief Open a new session
 *
 * @param client_nonce Nonce provided by the client
 * @param out_session_id Pointer to receive the generated session_id
 * @param out_lease_ms Pointer to receive the lease duration
 * @return ESP_OK on success
 */
esp_err_t session_mgr_open(uint32_t client_nonce, uint32_t *out_session_id, uint16_t *out_lease_ms);

/**
 * @brief Refresh an existing session (KEEPALIVE)
 *
 * @param session_id The session ID to refresh
 * @return ESP_OK if valid session, ESP_ERR_INVALID_ARG if session doesn't match
 */
esp_err_t session_mgr_keepalive(uint32_t session_id);

/**
 * @brief Close the current session
 *
 * @param session_id The session ID to close (must match current)
 * @return ESP_OK on success
 */
esp_err_t session_mgr_close(uint32_t session_id);

/**
 * @brief Check if the current session is valid (for gating commands)
 *
 * @param session_id The session ID to validate
 * @return true if session is LIVE and matches
 */
bool session_mgr_is_valid(uint32_t session_id);

/**
 * @brief Check if any session is currently live
 *
 * @return true if there's an active, non-stale session
 */
bool session_mgr_is_live(void);

/**
 * @brief Get current session state
 *
 * @return Current session state
 */
session_state_t session_mgr_get_state(void);

/**
 * @brief Get current session info (for debugging/telemetry)
 *
 * @param out_info Pointer to receive session info
 * @return ESP_OK if session exists, ESP_ERR_NOT_FOUND otherwise
 */
esp_err_t session_mgr_get_info(session_info_t *out_info);

/**
 * @brief Check for lease expiry and update state
 *
 * Call this periodically (e.g., from telemetry task) to detect stale sessions.
 * Returns true if state changed from LIVE to STALE.
 */
bool session_mgr_check_expiry(void);

/**
 * @brief Force-expire the current session (e.g., on BLE disconnect)
 */
void session_mgr_force_expire(void);

#ifdef __cplusplus
}
#endif
