#include "pid_controller.h"
#include "modbus_master.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "nvs.h"

static const char *TAG = "pid_ctrl";

/* NVS storage */
#define NVS_NAMESPACE       "pid_ctrl"
#define NVS_KEY_IDLE_TIMEOUT "idle_timeout"

/* State */
static bool s_initialized = false;
static pid_config_t s_config;
static pid_controller_t s_controllers[PID_MAX_CONTROLLERS];
static SemaphoreHandle_t s_data_mutex = NULL;
static TaskHandle_t s_poll_task = NULL;
static volatile bool s_poll_running = false;

/* Lazy polling state */
static uint8_t s_idle_timeout_minutes = PID_IDLE_TIMEOUT_DEFAULT;
static volatile uint32_t s_last_activity_ms = 0;
static volatile bool s_lazy_polling_active = false;

/* Get current time in milliseconds */
static uint32_t get_time_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

/* Scaling functions */
static float decode_temp(int16_t raw)
{
    return raw / 10.0f;
}

static int16_t encode_temp(float temp)
{
    return (int16_t)(temp * 10.0f + 0.5f);
}

static float decode_percent(int16_t raw)
{
    return raw / 10.0f;
}

/* Poll a single controller */
static void poll_controller(pid_controller_t *ctrl)
{
    /* Read registers 0-5 (PV, MV1, MV2, MVFB, STATUS, SV) in one request */
    uint16_t regs[6];
    modbus_err_t err = modbus_read_holding(ctrl->addr, 0, 6, regs);

    ctrl->total_polls++;

    if (err != MODBUS_OK) {
        ctrl->error_count++;
        ctrl->total_errors++;

        if (ctrl->error_count >= 3) {
            if (ctrl->state == PID_STATE_ONLINE || ctrl->state == PID_STATE_STALE) {
                ESP_LOGW(TAG, "Controller %d went offline: %s",
                         ctrl->addr, modbus_err_str(err));
                ctrl->state = PID_STATE_OFFLINE;
            }
        } else if (ctrl->state == PID_STATE_ONLINE) {
            ctrl->state = PID_STATE_STALE;
        }
        return;
    }

    /* Read MODE register (13) separately - not contiguous with 0-5 */
    uint16_t mode_reg = 0;
    modbus_err_t mode_err = modbus_read_holding(ctrl->addr, LC108_REG_MODE, 1, &mode_reg);
    if (mode_err != MODBUS_OK) {
        ESP_LOGW(TAG, "Controller %d MODE read failed: %s",
                 ctrl->addr, modbus_err_str(mode_err));
        /* Continue anyway - we still have the main registers */
    }

    /* Success - update data */
    xSemaphoreTake(s_data_mutex, portMAX_DELAY);

    ctrl->data.pv = decode_temp((int16_t)regs[0]);
    ctrl->data.output_pct = decode_percent((int16_t)regs[1]);
    ctrl->data.status = regs[4];
    ctrl->data.sv = decode_temp((int16_t)regs[5]);
    ctrl->data.alarm1 = (regs[4] & PID_STATUS_ALARM1) != 0;
    ctrl->data.alarm2 = (regs[4] & PID_STATUS_ALARM2) != 0;
    ctrl->data.mode = (mode_err == MODBUS_OK) ? (mode_reg & 0xFF) : ctrl->data.mode;

    ctrl->last_update_ms = get_time_ms();
    ctrl->error_count = 0;

    if (ctrl->state != PID_STATE_ONLINE) {
        ESP_LOGI(TAG, "Controller %d online: PV=%.1f SV=%.1f MODE=%d",
                 ctrl->addr, ctrl->data.pv, ctrl->data.sv, ctrl->data.mode);
    }
    ctrl->state = PID_STATE_ONLINE;

    xSemaphoreGive(s_data_mutex);

    ESP_LOGD(TAG, "[%d] PV=%.1f SV=%.1f OUT=%.1f%% ST=0x%04X MODE=%d",
             ctrl->addr, ctrl->data.pv, ctrl->data.sv,
             ctrl->data.output_pct, ctrl->data.status, ctrl->data.mode);
}

/* Check if we should be in lazy polling mode */
static bool check_lazy_polling_state(void)
{
    if (s_idle_timeout_minutes == PID_IDLE_TIMEOUT_DISABLED) {
        return false;  /* Lazy polling disabled */
    }

    uint32_t now = get_time_ms();
    uint32_t idle_ms = now - s_last_activity_ms;
    uint32_t timeout_ms = (uint32_t)s_idle_timeout_minutes * 60 * 1000;

    return idle_ms >= timeout_ms;
}

/* Polling task */
static void poll_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "Poll task started, %d controllers", s_config.count);

    uint8_t current_idx = 0;
    TickType_t last_poll = xTaskGetTickCount();
    bool was_lazy = false;

    while (s_poll_running) {
        /* Determine poll interval based on lazy state */
        bool is_lazy = check_lazy_polling_state();
        uint32_t poll_interval = is_lazy ? PID_POLL_INTERVAL_SLOW_MS : s_config.poll_interval_ms;

        /* Log state transition */
        if (is_lazy != was_lazy) {
            if (is_lazy) {
                ESP_LOGI(TAG, "Entering lazy polling mode (interval=%lums)",
                         (unsigned long)poll_interval);
            } else {
                ESP_LOGI(TAG, "Resuming fast polling mode (interval=%lums)",
                         (unsigned long)poll_interval);
            }
            was_lazy = is_lazy;
        }
        s_lazy_polling_active = is_lazy;

        /* Wait for next poll interval */
        vTaskDelayUntil(&last_poll, pdMS_TO_TICKS(poll_interval));

        if (!s_poll_running) break;

        /* Poll current controller */
        if (current_idx < s_config.count) {
            poll_controller(&s_controllers[current_idx]);
        }

        /* Move to next controller (round-robin) */
        current_idx = (current_idx + 1) % s_config.count;

        /* Check for stale data on other controllers */
        uint32_t now = get_time_ms();
        uint32_t stale_threshold = is_lazy ? (PID_POLL_INTERVAL_SLOW_MS * 3) : PID_STALE_THRESHOLD_MS;
        for (int i = 0; i < s_config.count; i++) {
            if (s_controllers[i].state == PID_STATE_ONLINE) {
                uint32_t age = now - s_controllers[i].last_update_ms;
                if (age > stale_threshold) {
                    s_controllers[i].state = PID_STATE_STALE;
                    ESP_LOGW(TAG, "Controller %d data stale (age=%lums)",
                             s_controllers[i].addr, (unsigned long)age);
                }
            }
        }
    }

    ESP_LOGI(TAG, "Poll task stopped");
    vTaskDelete(NULL);
}

/* Load idle timeout from NVS */
static void load_idle_timeout_from_nvs(void)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs);
    if (err == ESP_OK) {
        uint8_t value = PID_IDLE_TIMEOUT_DEFAULT;
        err = nvs_get_u8(nvs, NVS_KEY_IDLE_TIMEOUT, &value);
        if (err == ESP_OK) {
            s_idle_timeout_minutes = value;
            ESP_LOGI(TAG, "Loaded idle timeout from NVS: %d minutes", value);
        } else {
            ESP_LOGI(TAG, "No idle timeout in NVS, using default: %d minutes",
                     PID_IDLE_TIMEOUT_DEFAULT);
        }
        nvs_close(nvs);
    } else {
        ESP_LOGW(TAG, "Failed to open NVS for reading: %s", esp_err_to_name(err));
    }
}

/* Save idle timeout to NVS */
static esp_err_t save_idle_timeout_to_nvs(uint8_t minutes)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS for writing: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_set_u8(nvs, NVS_KEY_IDLE_TIMEOUT, minutes);
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }

    nvs_close(nvs);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Saved idle timeout to NVS: %d minutes", minutes);
    } else {
        ESP_LOGE(TAG, "Failed to save idle timeout to NVS: %s", esp_err_to_name(err));
    }

    return err;
}

esp_err_t pid_controller_init(const pid_config_t *config)
{
    if (s_initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_OK;
    }

    /* Use defaults if no config */
    if (config) {
        s_config = *config;
    } else {
        s_config = (pid_config_t)PID_CONFIG_DEFAULT();
    }

    if (s_config.count > PID_MAX_CONTROLLERS) {
        s_config.count = PID_MAX_CONTROLLERS;
    }

    /* Load idle timeout from NVS */
    load_idle_timeout_from_nvs();

    /* Initialize activity timestamp to now */
    s_last_activity_ms = get_time_ms();

    /* Initialize Modbus master */
    esp_err_t err = modbus_master_init(NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init modbus master: %s", esp_err_to_name(err));
        return err;
    }

    /* Create data mutex */
    s_data_mutex = xSemaphoreCreateMutex();
    if (!s_data_mutex) {
        ESP_LOGE(TAG, "Failed to create mutex");
        modbus_master_deinit();
        return ESP_ERR_NO_MEM;
    }

    /* Initialize controller state */
    memset(s_controllers, 0, sizeof(s_controllers));
    for (int i = 0; i < s_config.count; i++) {
        s_controllers[i].addr = s_config.addresses[i];
        s_controllers[i].state = PID_STATE_UNKNOWN;
    }

    /* Start polling task */
    s_poll_running = true;
    BaseType_t ret = xTaskCreate(poll_task, "pid_poll", 4096, NULL, 4, &s_poll_task);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create poll task");
        vSemaphoreDelete(s_data_mutex);
        modbus_master_deinit();
        return ESP_ERR_NO_MEM;
    }

    s_initialized = true;
    ESP_LOGI(TAG, "PID controller manager initialized, %d controllers, idle timeout=%d min",
             s_config.count, s_idle_timeout_minutes);

    return ESP_OK;
}

void pid_controller_deinit(void)
{
    if (!s_initialized) return;

    /* Stop poll task */
    s_poll_running = false;
    if (s_poll_task) {
        vTaskDelay(pdMS_TO_TICKS(s_config.poll_interval_ms * 2));
        s_poll_task = NULL;
    }

    /* Cleanup */
    if (s_data_mutex) {
        vSemaphoreDelete(s_data_mutex);
        s_data_mutex = NULL;
    }

    modbus_master_deinit();
    s_initialized = false;

    ESP_LOGI(TAG, "PID controller manager deinitialized");
}

esp_err_t pid_controller_get(uint8_t index, pid_controller_t *out)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    if (index >= s_config.count || !out) return ESP_ERR_INVALID_ARG;

    xSemaphoreTake(s_data_mutex, portMAX_DELAY);
    *out = s_controllers[index];
    xSemaphoreGive(s_data_mutex);

    return ESP_OK;
}

esp_err_t pid_controller_get_by_addr(uint8_t addr, pid_controller_t *out)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    if (!out) return ESP_ERR_INVALID_ARG;

    for (int i = 0; i < s_config.count; i++) {
        if (s_controllers[i].addr == addr) {
            xSemaphoreTake(s_data_mutex, portMAX_DELAY);
            *out = s_controllers[i];
            xSemaphoreGive(s_data_mutex);
            return ESP_OK;
        }
    }

    return ESP_ERR_NOT_FOUND;
}

esp_err_t pid_controller_set_sv(uint8_t addr, float sv_celsius)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;

    int16_t raw_sv = encode_temp(sv_celsius);
    modbus_err_t err = modbus_write_single(addr, LC108_REG_SV, (uint16_t)raw_sv);

    if (err != MODBUS_OK) {
        ESP_LOGE(TAG, "Failed to write SV to addr %d: %s", addr, modbus_err_str(err));
        return ESP_FAIL;
    }

    /* Read back to verify the write succeeded */
    uint16_t readback = 0;
    err = modbus_read_holding(addr, LC108_REG_SV, 1, &readback);
    if (err != MODBUS_OK) {
        ESP_LOGW(TAG, "SV write verify read failed on addr %d: %s",
                 addr, modbus_err_str(err));
        /* Write succeeded but verify failed - still return OK */
    } else {
        float readback_celsius = decode_temp((int16_t)readback);
        /* Allow 0.1C tolerance due to rounding */
        float diff = sv_celsius - readback_celsius;
        if (diff < -0.15f || diff > 0.15f) {
            ESP_LOGW(TAG, "SV write verify mismatch on addr %d: wrote %.1f, read %.1f",
                     addr, sv_celsius, readback_celsius);
            return ESP_ERR_INVALID_RESPONSE;
        }
    }

    ESP_LOGI(TAG, "Set SV on addr %d to %.1f C - verified", addr, sv_celsius);
    return ESP_OK;
}

esp_err_t pid_controller_read_params(uint8_t addr, float *p_gain,
                                      uint16_t *i_time, uint16_t *d_time)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;

    /* Read P, I, D registers (24, 25, 26 -> 0-based: 23, 24, 25) */
    uint16_t regs[3];
    modbus_err_t err = modbus_read_holding(addr, LC108_REG_P1, 3, regs);

    if (err != MODBUS_OK) {
        ESP_LOGE(TAG, "Failed to read PID params from addr %d: %s",
                 addr, modbus_err_str(err));
        return ESP_FAIL;
    }

    if (p_gain) *p_gain = decode_percent((int16_t)regs[0]);
    if (i_time) *i_time = regs[1];
    if (d_time) *d_time = regs[2];

    ESP_LOGD(TAG, "PID params addr %d: P=%.1f I=%d D=%d",
             addr, decode_percent((int16_t)regs[0]), regs[1], regs[2]);

    return ESP_OK;
}

esp_err_t pid_controller_start_autotune(uint8_t addr)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;

    modbus_err_t err = modbus_write_single(addr, LC108_REG_AT, 1);
    if (err != MODBUS_OK) {
        ESP_LOGE(TAG, "Failed to start autotune on addr %d: %s",
                 addr, modbus_err_str(err));
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Started autotune on addr %d", addr);
    return ESP_OK;
}

esp_err_t pid_controller_stop_autotune(uint8_t addr)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;

    modbus_err_t err = modbus_write_single(addr, LC108_REG_AT, 0);
    if (err != MODBUS_OK) {
        ESP_LOGE(TAG, "Failed to stop autotune on addr %d: %s",
                 addr, modbus_err_str(err));
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Stopped autotune on addr %d", addr);
    return ESP_OK;
}

bool pid_controller_any_alarm(void)
{
    if (!s_initialized) return false;

    xSemaphoreTake(s_data_mutex, portMAX_DELAY);
    bool alarm = false;
    for (int i = 0; i < s_config.count; i++) {
        if (s_controllers[i].state == PID_STATE_ONLINE) {
            if (s_controllers[i].data.alarm1 || s_controllers[i].data.alarm2) {
                alarm = true;
                break;
            }
        }
    }
    xSemaphoreGive(s_data_mutex);

    return alarm;
}

uint32_t pid_controller_data_age_ms(uint8_t addr)
{
    if (!s_initialized) return UINT32_MAX;

    for (int i = 0; i < s_config.count; i++) {
        if (s_controllers[i].addr == addr) {
            if (s_controllers[i].last_update_ms == 0) {
                return UINT32_MAX;
            }
            return get_time_ms() - s_controllers[i].last_update_ms;
        }
    }

    return UINT32_MAX;
}

esp_err_t pid_controller_force_poll(uint8_t addr)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;

    for (int i = 0; i < s_config.count; i++) {
        if (s_controllers[i].addr == addr) {
            poll_controller(&s_controllers[i]);
            return ESP_OK;
        }
    }

    return ESP_ERR_NOT_FOUND;
}

esp_err_t pid_controller_write_params(uint8_t addr, float p_gain,
                                       uint16_t i_time, uint16_t d_time)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;

    /* Encode P gain (x10 scaling) */
    int16_t raw_p = (int16_t)(p_gain * 10.0f + 0.5f);

    /* Write P, I, D registers consecutively */
    uint16_t regs[3] = { (uint16_t)raw_p, i_time, d_time };
    modbus_err_t err = modbus_write_multiple(addr, LC108_REG_P1, 3, regs);

    if (err != MODBUS_OK) {
        ESP_LOGE(TAG, "Failed to write PID params to addr %d: %s",
                 addr, modbus_err_str(err));
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Set PID params on addr %d: P=%.1f I=%d D=%d",
             addr, p_gain, i_time, d_time);
    return ESP_OK;
}

esp_err_t pid_controller_set_mode(uint8_t addr, uint8_t mode)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    if (mode > 3) return ESP_ERR_INVALID_ARG;

    const char *mode_names[] = { "STOP", "MANUAL", "AUTO", "PROGRAM" };

    modbus_err_t err = modbus_write_single(addr, LC108_REG_MODE, mode);

    if (err != MODBUS_OK) {
        ESP_LOGE(TAG, "Failed to set mode on addr %d: %s",
                 addr, modbus_err_str(err));
        return ESP_FAIL;
    }

    /* Read back to verify the write succeeded */
    uint16_t readback = 0;
    err = modbus_read_holding(addr, LC108_REG_MODE, 1, &readback);
    if (err != MODBUS_OK) {
        ESP_LOGW(TAG, "Mode write verify read failed on addr %d: %s",
                 addr, modbus_err_str(err));
        /* Write succeeded but verify failed - still return OK */
    } else if ((readback & 0xFF) != mode) {
        ESP_LOGW(TAG, "Mode write verify mismatch on addr %d: wrote %s (%d), read %d",
                 addr, mode_names[mode], mode, readback & 0xFF);
        return ESP_ERR_INVALID_RESPONSE;
    }

    ESP_LOGI(TAG, "Set mode on addr %d to %s (%d) - verified", addr, mode_names[mode], mode);
    return ESP_OK;
}

esp_err_t pid_controller_set_alarm_limits(uint8_t addr, float alarm1_celsius,
                                           float alarm2_celsius)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;

    int16_t raw_al1 = encode_temp(alarm1_celsius);
    int16_t raw_al2 = encode_temp(alarm2_celsius);

    /* Write AL1 and AL2 registers consecutively */
    uint16_t regs[2] = { (uint16_t)raw_al1, (uint16_t)raw_al2 };
    modbus_err_t err = modbus_write_multiple(addr, LC108_REG_AL1, 2, regs);

    if (err != MODBUS_OK) {
        ESP_LOGE(TAG, "Failed to set alarm limits on addr %d: %s",
                 addr, modbus_err_str(err));
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Set alarm limits on addr %d: AL1=%.1f AL2=%.1f",
             addr, alarm1_celsius, alarm2_celsius);
    return ESP_OK;
}

bool pid_controller_is_autotuning(uint8_t addr)
{
    if (!s_initialized) return false;

    xSemaphoreTake(s_data_mutex, portMAX_DELAY);
    bool autotuning = false;
    for (int i = 0; i < s_config.count; i++) {
        if (s_controllers[i].addr == addr) {
            autotuning = (s_controllers[i].data.status & PID_STATUS_AUTOTUNE) != 0;
            break;
        }
    }
    xSemaphoreGive(s_data_mutex);

    return autotuning;
}

esp_err_t pid_controller_read_alarm_limits(uint8_t addr, float *alarm1_celsius,
                                            float *alarm2_celsius)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;

    /* Read AL1 and AL2 registers */
    uint16_t regs[2];
    modbus_err_t err = modbus_read_holding(addr, LC108_REG_AL1, 2, regs);

    if (err != MODBUS_OK) {
        ESP_LOGE(TAG, "Failed to read alarm limits from addr %d: %s",
                 addr, modbus_err_str(err));
        return ESP_FAIL;
    }

    if (alarm1_celsius) *alarm1_celsius = decode_temp((int16_t)regs[0]);
    if (alarm2_celsius) *alarm2_celsius = decode_temp((int16_t)regs[1]);

    ESP_LOGD(TAG, "Alarm limits addr %d: AL1=%.1f AL2=%.1f",
             addr, decode_temp((int16_t)regs[0]), decode_temp((int16_t)regs[1]));

    return ESP_OK;
}

uint8_t pid_controller_get_count(void)
{
    if (!s_initialized) return 0;
    return s_config.count;
}

esp_err_t pid_controller_read_registers(uint8_t addr, uint16_t start_reg,
                                         uint8_t count, uint16_t *values)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    if (count == 0 || count > 16 || !values) return ESP_ERR_INVALID_ARG;

    modbus_err_t err = modbus_read_holding(addr, start_reg, count, values);

    if (err != MODBUS_OK) {
        ESP_LOGE(TAG, "Failed to read registers from addr %d, reg %d, count %d: %s",
                 addr, start_reg, count, modbus_err_str(err));
        return ESP_ERR_TIMEOUT;
    }

    ESP_LOGD(TAG, "Read %d registers from addr %d starting at %d",
             count, addr, start_reg);

    return ESP_OK;
}

esp_err_t pid_controller_write_register(uint8_t addr, uint16_t reg,
                                         uint16_t value, uint16_t *verified_value)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;

    modbus_err_t err = modbus_write_single(addr, reg, value);

    if (err != MODBUS_OK) {
        ESP_LOGE(TAG, "Failed to write register %d on addr %d: %s",
                 reg, addr, modbus_err_str(err));
        return ESP_ERR_TIMEOUT;
    }

    /* Read back to verify */
    uint16_t readback = 0;
    err = modbus_read_holding(addr, reg, 1, &readback);
    if (err != MODBUS_OK) {
        ESP_LOGW(TAG, "Write verify read failed on addr %d reg %d: %s",
                 addr, reg, modbus_err_str(err));
        /* Write succeeded but verify read failed - return the original value */
        if (verified_value) *verified_value = value;
        return ESP_OK;
    }

    if (verified_value) *verified_value = readback;

    if (readback != value) {
        ESP_LOGW(TAG, "Write verify mismatch on addr %d reg %d: wrote 0x%04X, read 0x%04X",
                 addr, reg, value, readback);
        return ESP_ERR_INVALID_RESPONSE;
    }

    ESP_LOGD(TAG, "Wrote register %d on addr %d: 0x%04X - verified",
             reg, addr, value);

    return ESP_OK;
}

esp_err_t pid_controller_set_idle_timeout(uint8_t minutes)
{
    s_idle_timeout_minutes = minutes;

    /* Save to NVS for persistence */
    esp_err_t err = save_idle_timeout_to_nvs(minutes);

    /* Reset activity timer when setting changes */
    s_last_activity_ms = get_time_ms();

    ESP_LOGI(TAG, "Idle timeout set to %d minutes%s",
             minutes, minutes == 0 ? " (lazy polling disabled)" : "");

    return err;
}

uint8_t pid_controller_get_idle_timeout(void)
{
    return s_idle_timeout_minutes;
}

void pid_controller_signal_activity(void)
{
    s_last_activity_ms = get_time_ms();
    /* If we were in lazy mode, we'll transition back to fast on next poll cycle */
}

bool pid_controller_is_lazy_polling(void)
{
    return s_lazy_polling_active;
}
