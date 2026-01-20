#include "session_mgr.h"

#include <string.h>
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"

static const char *TAG = "session_mgr";

static session_info_t s_session = {0};

esp_err_t session_mgr_init(void)
{
    memset(&s_session, 0, sizeof(s_session));
    s_session.state = SESSION_STATE_NONE;
    ESP_LOGI(TAG, "Session manager initialized");
    return ESP_OK;
}

esp_err_t session_mgr_open(uint32_t client_nonce, uint32_t *out_session_id, uint16_t *out_lease_ms)
{
    // Generate random session ID
    uint32_t new_id;
    do {
        new_id = esp_random();
    } while (new_id == 0);  // Avoid 0 as session ID

    s_session.session_id = new_id;
    s_session.client_nonce = client_nonce;
    s_session.lease_ms = SESSION_DEFAULT_LEASE_MS;
    s_session.last_keepalive_us = esp_timer_get_time();
    s_session.state = SESSION_STATE_LIVE;

    if (out_session_id) {
        *out_session_id = new_id;
    }
    if (out_lease_ms) {
        *out_lease_ms = s_session.lease_ms;
    }

    ESP_LOGI(TAG, "Session opened: id=0x%08lx nonce=0x%08lx lease=%ums",
             (unsigned long)new_id, (unsigned long)client_nonce, s_session.lease_ms);

    return ESP_OK;
}

esp_err_t session_mgr_keepalive(uint32_t session_id)
{
    if (s_session.state == SESSION_STATE_NONE) {
        ESP_LOGW(TAG, "KEEPALIVE rejected: no active session");
        return ESP_ERR_INVALID_STATE;
    }

    if (s_session.session_id != session_id) {
        ESP_LOGW(TAG, "KEEPALIVE rejected: session mismatch (got 0x%08lx, expected 0x%08lx)",
                 (unsigned long)session_id, (unsigned long)s_session.session_id);
        return ESP_ERR_INVALID_ARG;
    }

    s_session.last_keepalive_us = esp_timer_get_time();

    // Revive stale session on valid keepalive
    if (s_session.state == SESSION_STATE_STALE) {
        ESP_LOGI(TAG, "Session revived from STALE to LIVE");
        s_session.state = SESSION_STATE_LIVE;
    }

    return ESP_OK;
}

esp_err_t session_mgr_close(uint32_t session_id)
{
    if (s_session.state == SESSION_STATE_NONE) {
        return ESP_ERR_INVALID_STATE;
    }

    if (s_session.session_id != session_id) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Session closed: id=0x%08lx", (unsigned long)session_id);
    memset(&s_session, 0, sizeof(s_session));
    s_session.state = SESSION_STATE_NONE;

    return ESP_OK;
}

bool session_mgr_is_valid(uint32_t session_id)
{
    return (s_session.state == SESSION_STATE_LIVE &&
            s_session.session_id == session_id);
}

bool session_mgr_is_live(void)
{
    return (s_session.state == SESSION_STATE_LIVE);
}

session_state_t session_mgr_get_state(void)
{
    return s_session.state;
}

esp_err_t session_mgr_get_info(session_info_t *out_info)
{
    if (!out_info) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_session.state == SESSION_STATE_NONE) {
        return ESP_ERR_NOT_FOUND;
    }

    memcpy(out_info, &s_session, sizeof(session_info_t));
    return ESP_OK;
}

bool session_mgr_check_expiry(void)
{
    if (s_session.state != SESSION_STATE_LIVE) {
        return false;
    }

    int64_t now_us = esp_timer_get_time();
    int64_t elapsed_us = now_us - s_session.last_keepalive_us;
    int64_t lease_us = (int64_t)s_session.lease_ms * 1000;
    int64_t grace_us = (int64_t)SESSION_GRACE_PERIOD_MS * 1000;

    if (elapsed_us > (lease_us + grace_us)) {
        ESP_LOGW(TAG, "Session expired: id=0x%08lx (elapsed=%lldms, lease=%ums)",
                 (unsigned long)s_session.session_id,
                 (long long)(elapsed_us / 1000),
                 s_session.lease_ms);
        s_session.state = SESSION_STATE_STALE;
        return true;
    }

    return false;
}

void session_mgr_force_expire(void)
{
    if (s_session.state != SESSION_STATE_NONE) {
        ESP_LOGW(TAG, "Session force-expired: id=0x%08lx", (unsigned long)s_session.session_id);
        memset(&s_session, 0, sizeof(s_session));
        s_session.state = SESSION_STATE_NONE;
    }
}
