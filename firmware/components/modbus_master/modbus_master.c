#include "modbus_master.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "modbus";

/* Driver state */
static bool s_initialized = false;
static SemaphoreHandle_t s_bus_mutex = NULL;
static modbus_config_t s_config;
static int64_t s_last_transaction_us = 0;

/* RX buffer */
#define RX_BUF_SIZE 256
static uint8_t s_rx_buf[RX_BUF_SIZE];

/* CRC-16 lookup table (polynomial 0xA001) */
static const uint16_t crc_table[256] = {
    0x0000, 0xC0C1, 0xC181, 0x0140, 0xC301, 0x03C0, 0x0280, 0xC241,
    0xC601, 0x06C0, 0x0780, 0xC741, 0x0500, 0xC5C1, 0xC481, 0x0440,
    0xCC01, 0x0CC0, 0x0D80, 0xCD41, 0x0F00, 0xCFC1, 0xCE81, 0x0E40,
    0x0A00, 0xCAC1, 0xCB81, 0x0B40, 0xC901, 0x09C0, 0x0880, 0xC841,
    0xD801, 0x18C0, 0x1980, 0xD941, 0x1B00, 0xDBC1, 0xDA81, 0x1A40,
    0x1E00, 0xDEC1, 0xDF81, 0x1F40, 0xDD01, 0x1DC0, 0x1C80, 0xDC41,
    0x1400, 0xD4C1, 0xD581, 0x1540, 0xD701, 0x17C0, 0x1680, 0xD641,
    0xD201, 0x12C0, 0x1380, 0xD341, 0x1100, 0xD1C1, 0xD081, 0x1040,
    0xF001, 0x30C0, 0x3180, 0xF141, 0x3300, 0xF3C1, 0xF281, 0x3240,
    0x3600, 0xF6C1, 0xF781, 0x3740, 0xF501, 0x35C0, 0x3480, 0xF441,
    0x3C00, 0xFCC1, 0xFD81, 0x3D40, 0xFF01, 0x3FC0, 0x3E80, 0xFE41,
    0xFA01, 0x3AC0, 0x3B80, 0xFB41, 0x3900, 0xF9C1, 0xF881, 0x3840,
    0x2800, 0xE8C1, 0xE981, 0x2940, 0xEB01, 0x2BC0, 0x2A80, 0xEA41,
    0xEE01, 0x2EC0, 0x2F80, 0xEF41, 0x2D00, 0xEDC1, 0xEC81, 0x2C40,
    0xE401, 0x24C0, 0x2580, 0xE541, 0x2700, 0xE7C1, 0xE681, 0x2640,
    0x2200, 0xE2C1, 0xE381, 0x2340, 0xE101, 0x21C0, 0x2080, 0xE041,
    0xA001, 0x60C0, 0x6180, 0xA141, 0x6300, 0xA3C1, 0xA281, 0x6240,
    0x6600, 0xA6C1, 0xA781, 0x6740, 0xA501, 0x65C0, 0x6480, 0xA441,
    0x6C00, 0xACC1, 0xAD81, 0x6D40, 0xAF01, 0x6FC0, 0x6E80, 0xAE41,
    0xAA01, 0x6AC0, 0x6B80, 0xAB41, 0x6900, 0xA9C1, 0xA881, 0x6840,
    0x7800, 0xB8C1, 0xB981, 0x7940, 0xBB01, 0x7BC0, 0x7A80, 0xBA41,
    0xBE01, 0x7EC0, 0x7F80, 0xBF41, 0x7D00, 0xBDC1, 0xBC81, 0x7C40,
    0xB401, 0x74C0, 0x7580, 0xB541, 0x7700, 0xB7C1, 0xB681, 0x7640,
    0x7200, 0xB2C1, 0xB381, 0x7340, 0xB101, 0x71C0, 0x7080, 0xB041,
    0x5000, 0x90C1, 0x9181, 0x5140, 0x9301, 0x53C0, 0x5280, 0x9241,
    0x9601, 0x56C0, 0x5780, 0x9741, 0x5500, 0x95C1, 0x9481, 0x5440,
    0x9C01, 0x5CC0, 0x5D80, 0x9D41, 0x5F00, 0x9FC1, 0x9E81, 0x5E40,
    0x5A00, 0x9AC1, 0x9B81, 0x5B40, 0x9901, 0x59C0, 0x5880, 0x9841,
    0x8801, 0x48C0, 0x4980, 0x8941, 0x4B00, 0x8BC1, 0x8A81, 0x4A40,
    0x4E00, 0x8EC1, 0x8F81, 0x4F40, 0x8D01, 0x4DC0, 0x4C80, 0x8C41,
    0x4400, 0x84C1, 0x8581, 0x4540, 0x8701, 0x47C0, 0x4680, 0x8641,
    0x8201, 0x42C0, 0x4380, 0x8341, 0x4100, 0x81C1, 0x8081, 0x4040,
};

uint16_t modbus_crc16(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc = (crc >> 8) ^ crc_table[(crc ^ data[i]) & 0xFF];
    }
    return crc;
}

static void wait_inter_frame_gap(void)
{
    int64_t now = esp_timer_get_time();
    int64_t elapsed_us = now - s_last_transaction_us;
    int64_t required_us = MODBUS_INTER_FRAME_MS * 1000;

    if (elapsed_us < required_us) {
        int64_t wait_us = required_us - elapsed_us;
        vTaskDelay(pdMS_TO_TICKS((wait_us + 999) / 1000));
    }
}

static void update_last_transaction(void)
{
    s_last_transaction_us = esp_timer_get_time();
}

esp_err_t modbus_master_init(const modbus_config_t *config)
{
    if (s_initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_OK;
    }

    /* Use defaults if no config provided */
    if (config) {
        s_config = *config;
    } else {
        s_config = (modbus_config_t)MODBUS_CONFIG_DEFAULT();
    }

    /* Create bus mutex */
    s_bus_mutex = xSemaphoreCreateMutex();
    if (!s_bus_mutex) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }

    /* Configure UART */
    uart_config_t uart_config = {
        .baud_rate = s_config.baud_rate,
        .data_bits = UART_DATA_8_BITS,
        .parity = MODBUS_DEFAULT_PARITY,
        .stop_bits = MODBUS_DEFAULT_STOP,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t err = uart_driver_install(MODBUS_UART_NUM, RX_BUF_SIZE * 2, 0, 0, NULL, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "UART driver install failed: %s", esp_err_to_name(err));
        vSemaphoreDelete(s_bus_mutex);
        return err;
    }

    err = uart_param_config(MODBUS_UART_NUM, &uart_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "UART config failed: %s", esp_err_to_name(err));
        uart_driver_delete(MODBUS_UART_NUM);
        vSemaphoreDelete(s_bus_mutex);
        return err;
    }

    err = uart_set_pin(MODBUS_UART_NUM, s_config.tx_gpio, s_config.rx_gpio,
                       UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "UART set pin failed: %s", esp_err_to_name(err));
        uart_driver_delete(MODBUS_UART_NUM);
        vSemaphoreDelete(s_bus_mutex);
        return err;
    }

    /* Configure RS-485 half-duplex mode */
    err = uart_set_mode(MODBUS_UART_NUM, UART_MODE_RS485_HALF_DUPLEX);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "UART RS-485 mode failed: %s", esp_err_to_name(err));
        uart_driver_delete(MODBUS_UART_NUM);
        vSemaphoreDelete(s_bus_mutex);
        return err;
    }

    /* Configure DE/RE pin if specified */
    if (s_config.de_gpio >= 0) {
        gpio_config_t de_conf = {
            .pin_bit_mask = (1ULL << s_config.de_gpio),
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        gpio_config(&de_conf);
        gpio_set_level(s_config.de_gpio, 0);  /* RX mode by default */
    }

    s_initialized = true;
    ESP_LOGI(TAG, "Modbus master initialized: baud=%d, TX=%d, RX=%d",
             s_config.baud_rate, s_config.tx_gpio, s_config.rx_gpio);

    return ESP_OK;
}

void modbus_master_deinit(void)
{
    if (!s_initialized) return;

    uart_driver_delete(MODBUS_UART_NUM);

    if (s_bus_mutex) {
        vSemaphoreDelete(s_bus_mutex);
        s_bus_mutex = NULL;
    }

    s_initialized = false;
    ESP_LOGI(TAG, "Modbus master deinitialized");
}

static modbus_err_t transact(const uint8_t *tx_frame, size_t tx_len,
                              uint8_t *rx_frame, size_t *rx_len,
                              size_t expected_min)
{
    if (!s_initialized) return MODBUS_ERR_NOT_INIT;

    wait_inter_frame_gap();

    /* Flush any stale RX data */
    uart_flush_input(MODBUS_UART_NUM);

    /* Enable TX if using manual DE control */
    if (s_config.de_gpio >= 0) {
        gpio_set_level(s_config.de_gpio, 1);
    }

    /* Transmit */
    int written = uart_write_bytes(MODBUS_UART_NUM, tx_frame, tx_len);
    if (written != tx_len) {
        ESP_LOGE(TAG, "TX failed: wrote %d of %d", written, (int)tx_len);
        if (s_config.de_gpio >= 0) gpio_set_level(s_config.de_gpio, 0);
        return MODBUS_ERR_FRAME;
    }

    /* Wait for TX to complete */
    esp_err_t err = uart_wait_tx_done(MODBUS_UART_NUM, pdMS_TO_TICKS(100));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "TX wait failed: %s", esp_err_to_name(err));
        if (s_config.de_gpio >= 0) gpio_set_level(s_config.de_gpio, 0);
        return MODBUS_ERR_FRAME;
    }

    /* Switch to RX mode */
    if (s_config.de_gpio >= 0) {
        gpio_set_level(s_config.de_gpio, 0);
    }

    /* Wait for response with timeout */
    int total_read = 0;
    TickType_t start = xTaskGetTickCount();
    TickType_t timeout_ticks = pdMS_TO_TICKS(s_config.response_timeout_ms);

    while ((xTaskGetTickCount() - start) < timeout_ticks) {
        int avail = 0;
        uart_get_buffered_data_len(MODBUS_UART_NUM, (size_t *)&avail);

        if (avail > 0) {
            int to_read = avail;
            if (total_read + to_read > RX_BUF_SIZE) {
                to_read = RX_BUF_SIZE - total_read;
            }
            int rd = uart_read_bytes(MODBUS_UART_NUM, s_rx_buf + total_read,
                                     to_read, pdMS_TO_TICKS(10));
            if (rd > 0) {
                total_read += rd;
            }

            /* Check if we have enough data */
            if (total_read >= (int)expected_min) {
                /* Wait a bit more for any trailing bytes */
                vTaskDelay(pdMS_TO_TICKS(5));
                uart_get_buffered_data_len(MODBUS_UART_NUM, (size_t *)&avail);
                if (avail > 0 && total_read + avail <= RX_BUF_SIZE) {
                    rd = uart_read_bytes(MODBUS_UART_NUM, s_rx_buf + total_read,
                                         avail, pdMS_TO_TICKS(10));
                    if (rd > 0) total_read += rd;
                }
                break;
            }
        } else {
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }

    update_last_transaction();

    if (total_read < (int)expected_min) {
        ESP_LOGW(TAG, "Timeout: got %d bytes, expected >= %d", total_read, (int)expected_min);
        return MODBUS_ERR_TIMEOUT;
    }

    /* Copy to output */
    memcpy(rx_frame, s_rx_buf, total_read);
    *rx_len = total_read;

    return MODBUS_OK;
}

modbus_err_t modbus_read_holding(uint8_t slave_addr, uint16_t reg_addr,
                                  uint16_t reg_count, uint16_t *data)
{
    if (!s_initialized) return MODBUS_ERR_NOT_INIT;
    if (slave_addr < 1 || slave_addr > 247) return MODBUS_ERR_INVALID_ADDR;
    if (reg_count < 1 || reg_count > MODBUS_MAX_REGISTERS) return MODBUS_ERR_INVALID_REG;

    if (xSemaphoreTake(s_bus_mutex, pdMS_TO_TICKS(500)) != pdTRUE) {
        return MODBUS_ERR_BUSY;
    }

    /* Build request: addr(1) + func(1) + reg_addr(2) + reg_count(2) + crc(2) = 8 */
    uint8_t tx_frame[8];
    tx_frame[0] = slave_addr;
    tx_frame[1] = MODBUS_FC_READ_HOLDING;
    tx_frame[2] = (reg_addr >> 8) & 0xFF;
    tx_frame[3] = reg_addr & 0xFF;
    tx_frame[4] = (reg_count >> 8) & 0xFF;
    tx_frame[5] = reg_count & 0xFF;
    uint16_t crc = modbus_crc16(tx_frame, 6);
    tx_frame[6] = crc & 0xFF;
    tx_frame[7] = (crc >> 8) & 0xFF;

    /* Expected response: addr(1) + func(1) + byte_count(1) + data(n*2) + crc(2) */
    size_t expected_len = 3 + (reg_count * 2) + 2;
    uint8_t rx_frame[MODBUS_MAX_ADU_SIZE];
    size_t rx_len = 0;

    modbus_err_t result = transact(tx_frame, 8, rx_frame, &rx_len, expected_len);

    if (result != MODBUS_OK) {
        xSemaphoreGive(s_bus_mutex);
        return result;
    }

    /* Verify response */
    if (rx_len < 5) {
        ESP_LOGW(TAG, "Response too short: %d bytes", (int)rx_len);
        xSemaphoreGive(s_bus_mutex);
        return MODBUS_ERR_FRAME;
    }

    /* Check for exception response */
    if (rx_frame[1] & 0x80) {
        ESP_LOGW(TAG, "Exception response: code %02X", rx_frame[2]);
        xSemaphoreGive(s_bus_mutex);
        return MODBUS_ERR_EXCEPTION;
    }

    /* Verify CRC */
    uint16_t rx_crc = rx_frame[rx_len - 2] | ((uint16_t)rx_frame[rx_len - 1] << 8);
    uint16_t calc_crc = modbus_crc16(rx_frame, rx_len - 2);
    if (rx_crc != calc_crc) {
        ESP_LOGW(TAG, "CRC mismatch: got %04X, expected %04X", rx_crc, calc_crc);
        xSemaphoreGive(s_bus_mutex);
        return MODBUS_ERR_CRC;
    }

    /* Verify address and function */
    if (rx_frame[0] != slave_addr || rx_frame[1] != MODBUS_FC_READ_HOLDING) {
        ESP_LOGW(TAG, "Address/function mismatch");
        xSemaphoreGive(s_bus_mutex);
        return MODBUS_ERR_FRAME;
    }

    /* Extract register data */
    uint8_t byte_count = rx_frame[2];
    if (byte_count != reg_count * 2) {
        ESP_LOGW(TAG, "Byte count mismatch: %d vs expected %d", byte_count, reg_count * 2);
        xSemaphoreGive(s_bus_mutex);
        return MODBUS_ERR_FRAME;
    }

    for (int i = 0; i < reg_count; i++) {
        data[i] = ((uint16_t)rx_frame[3 + i * 2] << 8) | rx_frame[4 + i * 2];
    }

    xSemaphoreGive(s_bus_mutex);

    ESP_LOGD(TAG, "Read %d registers from addr %d reg %d", reg_count, slave_addr, reg_addr);
    return MODBUS_OK;
}

modbus_err_t modbus_write_single(uint8_t slave_addr, uint16_t reg_addr, uint16_t value)
{
    if (!s_initialized) return MODBUS_ERR_NOT_INIT;
    if (slave_addr < 1 || slave_addr > 247) return MODBUS_ERR_INVALID_ADDR;

    if (xSemaphoreTake(s_bus_mutex, pdMS_TO_TICKS(500)) != pdTRUE) {
        return MODBUS_ERR_BUSY;
    }

    /* Build request: addr(1) + func(1) + reg_addr(2) + value(2) + crc(2) = 8 */
    uint8_t tx_frame[8];
    tx_frame[0] = slave_addr;
    tx_frame[1] = MODBUS_FC_WRITE_SINGLE;
    tx_frame[2] = (reg_addr >> 8) & 0xFF;
    tx_frame[3] = reg_addr & 0xFF;
    tx_frame[4] = (value >> 8) & 0xFF;
    tx_frame[5] = value & 0xFF;
    uint16_t crc = modbus_crc16(tx_frame, 6);
    tx_frame[6] = crc & 0xFF;
    tx_frame[7] = (crc >> 8) & 0xFF;

    /* Response is echo of request */
    uint8_t rx_frame[MODBUS_MAX_ADU_SIZE];
    size_t rx_len = 0;

    modbus_err_t result = transact(tx_frame, 8, rx_frame, &rx_len, 8);

    if (result != MODBUS_OK) {
        xSemaphoreGive(s_bus_mutex);
        return result;
    }

    /* Check for exception */
    if (rx_frame[1] & 0x80) {
        ESP_LOGW(TAG, "Exception response: code %02X", rx_frame[2]);
        xSemaphoreGive(s_bus_mutex);
        return MODBUS_ERR_EXCEPTION;
    }

    /* Verify CRC */
    uint16_t rx_crc = rx_frame[rx_len - 2] | ((uint16_t)rx_frame[rx_len - 1] << 8);
    uint16_t calc_crc = modbus_crc16(rx_frame, rx_len - 2);
    if (rx_crc != calc_crc) {
        ESP_LOGW(TAG, "CRC mismatch");
        xSemaphoreGive(s_bus_mutex);
        return MODBUS_ERR_CRC;
    }

    /* Verify echo */
    if (memcmp(tx_frame, rx_frame, 6) != 0) {
        ESP_LOGW(TAG, "Write echo mismatch");
        xSemaphoreGive(s_bus_mutex);
        return MODBUS_ERR_FRAME;
    }

    xSemaphoreGive(s_bus_mutex);

    ESP_LOGD(TAG, "Wrote register %d = %d on addr %d", reg_addr, value, slave_addr);
    return MODBUS_OK;
}

modbus_err_t modbus_write_multiple(uint8_t slave_addr, uint16_t reg_addr,
                                    uint16_t reg_count, const uint16_t *data)
{
    if (!s_initialized) return MODBUS_ERR_NOT_INIT;
    if (slave_addr < 1 || slave_addr > 247) return MODBUS_ERR_INVALID_ADDR;
    if (reg_count < 1 || reg_count > MODBUS_MAX_REGISTERS) return MODBUS_ERR_INVALID_REG;

    if (xSemaphoreTake(s_bus_mutex, pdMS_TO_TICKS(500)) != pdTRUE) {
        return MODBUS_ERR_BUSY;
    }

    /* Build request */
    uint8_t tx_frame[MODBUS_MAX_ADU_SIZE];
    tx_frame[0] = slave_addr;
    tx_frame[1] = MODBUS_FC_WRITE_MULTIPLE;
    tx_frame[2] = (reg_addr >> 8) & 0xFF;
    tx_frame[3] = reg_addr & 0xFF;
    tx_frame[4] = (reg_count >> 8) & 0xFF;
    tx_frame[5] = reg_count & 0xFF;
    tx_frame[6] = reg_count * 2;  /* Byte count */

    for (int i = 0; i < reg_count; i++) {
        tx_frame[7 + i * 2] = (data[i] >> 8) & 0xFF;
        tx_frame[8 + i * 2] = data[i] & 0xFF;
    }

    size_t tx_len = 7 + reg_count * 2;
    uint16_t crc = modbus_crc16(tx_frame, tx_len);
    tx_frame[tx_len++] = crc & 0xFF;
    tx_frame[tx_len++] = (crc >> 8) & 0xFF;

    /* Response: addr(1) + func(1) + reg_addr(2) + reg_count(2) + crc(2) = 8 */
    uint8_t rx_frame[MODBUS_MAX_ADU_SIZE];
    size_t rx_len = 0;

    modbus_err_t result = transact(tx_frame, tx_len, rx_frame, &rx_len, 8);

    if (result != MODBUS_OK) {
        xSemaphoreGive(s_bus_mutex);
        return result;
    }

    /* Check for exception */
    if (rx_frame[1] & 0x80) {
        ESP_LOGW(TAG, "Exception response: code %02X", rx_frame[2]);
        xSemaphoreGive(s_bus_mutex);
        return MODBUS_ERR_EXCEPTION;
    }

    /* Verify CRC */
    uint16_t rx_crc = rx_frame[rx_len - 2] | ((uint16_t)rx_frame[rx_len - 1] << 8);
    uint16_t calc_crc = modbus_crc16(rx_frame, rx_len - 2);
    if (rx_crc != calc_crc) {
        ESP_LOGW(TAG, "CRC mismatch");
        xSemaphoreGive(s_bus_mutex);
        return MODBUS_ERR_CRC;
    }

    xSemaphoreGive(s_bus_mutex);

    ESP_LOGD(TAG, "Wrote %d registers starting at %d on addr %d", reg_count, reg_addr, slave_addr);
    return MODBUS_OK;
}

const char *modbus_err_str(modbus_err_t err)
{
    switch (err) {
        case MODBUS_OK:             return "OK";
        case MODBUS_ERR_TIMEOUT:    return "Timeout";
        case MODBUS_ERR_CRC:        return "CRC error";
        case MODBUS_ERR_EXCEPTION:  return "Exception";
        case MODBUS_ERR_INVALID_ADDR: return "Invalid address";
        case MODBUS_ERR_INVALID_REG:  return "Invalid register";
        case MODBUS_ERR_FRAME:      return "Frame error";
        case MODBUS_ERR_BUSY:       return "Bus busy";
        case MODBUS_ERR_NOT_INIT:   return "Not initialized";
        default:                    return "Unknown";
    }
}
