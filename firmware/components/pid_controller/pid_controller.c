#include "pid_controller.h"
#include "modbus_master.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "pid_ctrl";

/* State */
static bool s_initialized = false;
static pid_config_t s_config;
static pid_controller_t s_controllers[PID_MAX_CONTROLLERS];
static SemaphoreHandle_t s_data_mutex = NULL;
static TaskHandle_t s_poll_task = NULL;
static volatile bool s_poll_running = false;

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

    /* Success - update data */
    xSemaphoreTake(s_data_mutex, portMAX_DELAY);

    ctrl->data.pv = decode_temp((int16_t)regs[0]);
    ctrl->data.output_pct = decode_percent((int16_t)regs[1]);
    ctrl->data.status = regs[4];
    ctrl->data.sv = decode_temp((int16_t)regs[5]);
    ctrl->data.alarm1 = (regs[4] & PID_STATUS_ALARM1) != 0;
    ctrl->data.alarm2 = (regs[4] & PID_STATUS_ALARM2) != 0;

    ctrl->last_update_ms = get_time_ms();
    ctrl->error_count = 0;

    if (ctrl->state != PID_STATE_ONLINE) {
        ESP_LOGI(TAG, "Controller %d online: PV=%.1f SV=%.1f",
                 ctrl->addr, ctrl->data.pv, ctrl->data.sv);
    }
    ctrl->state = PID_STATE_ONLINE;

    xSemaphoreGive(s_data_mutex);

    ESP_LOGD(TAG, "[%d] PV=%.1f SV=%.1f OUT=%.1f%% ST=0x%04X",
             ctrl->addr, ctrl->data.pv, ctrl->data.sv,
             ctrl->data.output_pct, ctrl->data.status);
}

/* Polling task */
static void poll_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "Poll task started, %d controllers", s_config.count);

    uint8_t current_idx = 0;
    TickType_t last_poll = xTaskGetTickCount();

    while (s_poll_running) {
        /* Wait for next poll interval */
        vTaskDelayUntil(&last_poll, pdMS_TO_TICKS(s_config.poll_interval_ms));

        if (!s_poll_running) break;

        /* Poll current controller */
        if (current_idx < s_config.count) {
            poll_controller(&s_controllers[current_idx]);
        }

        /* Move to next controller (round-robin) */
        current_idx = (current_idx + 1) % s_config.count;

        /* Check for stale data on other controllers */
        uint32_t now = get_time_ms();
        for (int i = 0; i < s_config.count; i++) {
            if (s_controllers[i].state == PID_STATE_ONLINE) {
                uint32_t age = now - s_controllers[i].last_update_ms;
                if (age > PID_STALE_THRESHOLD_MS) {
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
    ESP_LOGI(TAG, "PID controller manager initialized, %d controllers", s_config.count);

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

    ESP_LOGI(TAG, "Set SV on addr %d to %.1f C", addr, sv_celsius);
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
