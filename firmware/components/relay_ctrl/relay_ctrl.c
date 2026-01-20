#include "relay_ctrl.h"

#include <string.h>
#include "esp_log.h"
#include "driver/i2c_master.h"

static const char *TAG = "relay_ctrl";

/* I2C master bus handle */
static i2c_master_bus_handle_t s_i2c_bus = NULL;

/* I2C device handle for TCA9554 (relay outputs) */
static i2c_master_dev_handle_t s_tca9554_dev = NULL;

/* I2C device handle for TCA9534 (digital inputs) */
static i2c_master_dev_handle_t s_tca9534_dev = NULL;

/* Cached relay state (mirrors TCA9554 output register) */
static uint8_t s_relay_state = 0x00;

/* Flag to track initialization */
static bool s_initialized = false;

/* Flag to track if DI expander is available */
static bool s_di_available = false;

/**
 * @brief Write a byte to a TCA9554 register
 */
static esp_err_t tca9554_write_reg(uint8_t reg, uint8_t value)
{
    uint8_t write_buf[2] = { reg, value };
    esp_err_t ret = i2c_master_transmit(s_tca9554_dev, write_buf, sizeof(write_buf), 100);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write reg 0x%02X: %s", reg, esp_err_to_name(ret));
    }
    return ret;
}

/**
 * @brief Read a byte from a TCA9554 register
 */
static esp_err_t tca9554_read_reg(uint8_t reg, uint8_t *value)
{
    esp_err_t ret = i2c_master_transmit_receive(s_tca9554_dev, &reg, 1, value, 1, 100);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read reg 0x%02X: %s", reg, esp_err_to_name(ret));
    }
    return ret;
}

/**
 * @brief Read a byte from TCA9534 (digital input) register
 */
static esp_err_t tca9534_di_read_reg(uint8_t reg, uint8_t *value)
{
    if (s_tca9534_dev == NULL) {
        return ESP_ERR_NOT_FOUND;
    }
    esp_err_t ret = i2c_master_transmit_receive(s_tca9534_dev, &reg, 1, value, 1, 100);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "TCA9534: Failed to read reg 0x%02X: %s", reg, esp_err_to_name(ret));
    }
    return ret;
}

/**
 * @brief Write a byte to TCA9534 register
 */
static esp_err_t tca9534_di_write_reg(uint8_t reg, uint8_t value)
{
    if (s_tca9534_dev == NULL) {
        return ESP_ERR_NOT_FOUND;
    }
    uint8_t write_buf[2] = { reg, value };
    esp_err_t ret = i2c_master_transmit(s_tca9534_dev, write_buf, sizeof(write_buf), 100);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "TCA9534: Failed to write reg 0x%02X: %s", reg, esp_err_to_name(ret));
    }
    return ret;
}

esp_err_t relay_ctrl_init(void)
{
    if (s_initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing relay control (TCA9554 @ 0x%02X)", RELAY_TCA9554_ADDR);
    ESP_LOGI(TAG, "I2C: SDA=GPIO%d SCL=GPIO%d Freq=%dHz",
             RELAY_I2C_SDA_PIN, RELAY_I2C_SCL_PIN, RELAY_I2C_FREQ_HZ);

    /* Configure I2C master bus */
    i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = RELAY_I2C_SDA_PIN,
        .scl_io_num = RELAY_I2C_SCL_PIN,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .intr_priority = 0,
        .trans_queue_depth = 0,
        .flags = {
            .enable_internal_pullup = true,
        },
    };

    esp_err_t ret = i2c_new_master_bus(&bus_config, &s_i2c_bus);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create I2C bus: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Add TCA9554 device to the bus (relay outputs) */
    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = RELAY_TCA9554_ADDR,
        .scl_speed_hz = RELAY_I2C_FREQ_HZ,
    };

    ret = i2c_master_bus_add_device(s_i2c_bus, &dev_config, &s_tca9554_dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add TCA9554 device: %s", esp_err_to_name(ret));
        i2c_del_master_bus(s_i2c_bus);
        s_i2c_bus = NULL;
        return ret;
    }

    /* Probe the device to verify it's present */
    ret = i2c_master_probe(s_i2c_bus, RELAY_TCA9554_ADDR, 100);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "TCA9554 not found at address 0x%02X: %s",
                 RELAY_TCA9554_ADDR, esp_err_to_name(ret));
        i2c_master_bus_rm_device(s_tca9554_dev);
        i2c_del_master_bus(s_i2c_bus);
        s_tca9554_dev = NULL;
        s_i2c_bus = NULL;
        return ret;
    }

    ESP_LOGI(TAG, "TCA9554 found at address 0x%02X", RELAY_TCA9554_ADDR);

    /* Configure all 8 pins as outputs FIRST (write 0x00 to config register) */
    ret = tca9554_write_reg(TCA9554_REG_CONFIG, 0x00);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure pins as outputs");
        return ret;
    }

    /* Read the current output register state to see what hardware is doing */
    uint8_t current_output;
    ret = tca9554_read_reg(TCA9554_REG_OUTPUT, &current_output);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to read current output state, assuming 0x00");
        current_output = 0x00;
    }
    ESP_LOGI(TAG, "TCA9554 output register on boot: 0x%02X", current_output);

    /*
     * Initialize all outputs to OFF (safe state).
     * The TCA9554 datasheet says output register defaults to 0xFF on power-up,
     * so we explicitly set to 0x00 for a known safe state.
     */
    s_relay_state = 0x00;
    ret = tca9554_write_reg(TCA9554_REG_OUTPUT, s_relay_state);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize output register");
        return ret;
    }

    /* Verify configuration */
    uint8_t config_readback;
    ret = tca9554_read_reg(TCA9554_REG_CONFIG, &config_readback);
    if (ret == ESP_OK && config_readback == 0x00) {
        ESP_LOGI(TAG, "TCA9554 configured: all pins as outputs");
    } else {
        ESP_LOGW(TAG, "Config readback mismatch: expected 0x00, got 0x%02X", config_readback);
    }

    s_initialized = true;
    ESP_LOGI(TAG, "Relay control initialized - all relays OFF (ro_bits=0x00, convention: 1=ON, 0=OFF)");

    /* Now try to initialize the digital input expander (TCA9534 @ 0x21) */
    ESP_LOGI(TAG, "Probing for TCA9534 digital input expander @ 0x%02X", DI_TCA9534_ADDR);

    ret = i2c_master_probe(s_i2c_bus, DI_TCA9534_ADDR, 100);
    if (ret == ESP_OK) {
        /* Add TCA9534 device */
        i2c_device_config_t di_dev_config = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = DI_TCA9534_ADDR,
            .scl_speed_hz = RELAY_I2C_FREQ_HZ,
        };

        ret = i2c_master_bus_add_device(s_i2c_bus, &di_dev_config, &s_tca9534_dev);
        if (ret == ESP_OK) {
            /* Configure all pins as inputs (write 0xFF to config register) */
            ret = tca9534_di_write_reg(TCA9554_REG_CONFIG, 0xFF);
            if (ret == ESP_OK) {
                s_di_available = true;
                ESP_LOGI(TAG, "TCA9534 digital inputs initialized (DI1-DI8 available)");

                /* Read initial state */
                uint8_t di_state;
                if (tca9534_di_read_reg(TCA9554_REG_INPUT, &di_state) == ESP_OK) {
                    ESP_LOGI(TAG, "Initial DI state: 0x%02X", di_state);
                }
            } else {
                ESP_LOGW(TAG, "Failed to configure TCA9534 as inputs");
            }
        } else {
            ESP_LOGW(TAG, "Failed to add TCA9534 device: %s", esp_err_to_name(ret));
        }
    } else {
        ESP_LOGW(TAG, "TCA9534 not found at 0x%02X - digital inputs unavailable (using simulated)", DI_TCA9534_ADDR);
    }

    return ESP_OK;
}

esp_err_t relay_ctrl_set(uint8_t relay_index, uint8_t state)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "Not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (relay_index < 1 || relay_index > 8) {
        ESP_LOGE(TAG, "Invalid relay_index %u (must be 1-8)", relay_index);
        return ESP_ERR_INVALID_ARG;
    }

    if (state > 2) {
        ESP_LOGE(TAG, "Invalid state %u (must be 0=OFF, 1=ON, 2=TOGGLE)", state);
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t bit_mask = (1 << (relay_index - 1));
    uint8_t new_state = s_relay_state;

    switch (state) {
        case RELAY_STATE_OFF:
            new_state &= ~bit_mask;
            break;
        case RELAY_STATE_ON:
            new_state |= bit_mask;
            break;
        case RELAY_STATE_TOGGLE:
            new_state ^= bit_mask;
            break;
    }

    esp_err_t ret = tca9554_write_reg(TCA9554_REG_OUTPUT, new_state);
    if (ret == ESP_OK) {
        s_relay_state = new_state;
        ESP_LOGI(TAG, "Relay %u -> %s (ro_bits=0x%02X)",
                 relay_index,
                 (new_state & bit_mask) ? "ON" : "OFF",
                 new_state);
    }

    return ret;
}

esp_err_t relay_ctrl_set_mask(uint8_t mask, uint8_t values)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "Not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (mask == 0) {
        ESP_LOGW(TAG, "Mask is zero - no relays affected");
        return ESP_OK;
    }

    /* Atomic update: new = (current & ~mask) | (values & mask) */
    uint8_t new_state = (s_relay_state & ~mask) | (values & mask);

    esp_err_t ret = tca9554_write_reg(TCA9554_REG_OUTPUT, new_state);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Relay mask update: 0x%02X -> 0x%02X (mask=0x%02X values=0x%02X)",
                 s_relay_state, new_state, mask, values);
        s_relay_state = new_state;
    }

    return ret;
}

uint8_t relay_ctrl_get_state(void)
{
    return s_relay_state;
}

esp_err_t relay_ctrl_read_hw_state(uint8_t *state)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "Not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (state == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Read from output register to see what we're driving */
    esp_err_t ret = tca9554_read_reg(TCA9554_REG_OUTPUT, state);
    if (ret == ESP_OK) {
        ESP_LOGD(TAG, "Hardware output state: 0x%02X (cached: 0x%02X)", *state, s_relay_state);
    }
    return ret;
}

esp_err_t relay_ctrl_set_all(uint8_t state)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "Not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = tca9554_write_reg(TCA9554_REG_OUTPUT, state);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "All relays set: 0x%02X -> 0x%02X", s_relay_state, state);
        s_relay_state = state;
    }

    return ret;
}

esp_err_t relay_ctrl_all_off(void)
{
    return relay_ctrl_set_all(0x00);
}

esp_err_t relay_ctrl_read_di(uint8_t *di_bits)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "Not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (di_bits == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_di_available) {
        /* Return simulated safe state: all inputs HIGH (no alarms)
         * DI1 HIGH = E-stop not pressed
         * DI2 HIGH = Door closed
         * DI3 HIGH = LN2 present
         * DI4 LOW = No motor fault
         */
        *di_bits = 0x07;  /* DI1-DI3 HIGH, DI4-DI8 LOW */
        return ESP_OK;
    }

    /* Read from TCA9534 input register */
    return tca9534_di_read_reg(TCA9554_REG_INPUT, di_bits);
}

bool relay_ctrl_di_available(void)
{
    return s_di_available;
}
