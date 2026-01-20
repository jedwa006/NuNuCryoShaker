#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file modbus_master.h
 * @brief Modbus RTU Master for RS-485 communication
 *
 * Implements Modbus RTU master functionality for the ESP32-S3-ETH-8DI-8RO
 * board's onboard RS-485 transceiver. Supports reading and writing holding
 * registers with automatic CRC-16 calculation and half-duplex control.
 */

/* RS-485 Hardware Configuration */
#define MODBUS_UART_NUM         UART_NUM_2
#define MODBUS_TX_GPIO          17
#define MODBUS_RX_GPIO          18
#define MODBUS_DE_GPIO          (-1)    /* -1 if auto-controlled by transceiver */

/* Default Communication Settings */
#define MODBUS_DEFAULT_BAUD     9600
#define MODBUS_DEFAULT_PARITY   UART_PARITY_DISABLE
#define MODBUS_DEFAULT_STOP     UART_STOP_BITS_1

/* Timing (per Modbus RTU spec) */
#define MODBUS_T35_US           1750    /* 3.5 character times at 9600 baud */
#define MODBUS_RESPONSE_TIMEOUT_MS  100 /* Max wait for slave response */
#define MODBUS_INTER_FRAME_MS   5       /* Minimum gap between transactions */

/* Frame Limits */
#define MODBUS_MAX_PDU_SIZE     253
#define MODBUS_MAX_ADU_SIZE     256     /* 1 addr + 1 func + 253 data + 2 CRC */
#define MODBUS_MAX_REGISTERS    125     /* Max registers per read request */

/* Function Codes */
#define MODBUS_FC_READ_HOLDING      0x03
#define MODBUS_FC_WRITE_SINGLE      0x06
#define MODBUS_FC_WRITE_MULTIPLE    0x10

/* Error Codes */
typedef enum {
    MODBUS_OK = 0,
    MODBUS_ERR_TIMEOUT,         /* No response from slave */
    MODBUS_ERR_CRC,             /* CRC mismatch */
    MODBUS_ERR_EXCEPTION,       /* Slave returned exception */
    MODBUS_ERR_INVALID_ADDR,    /* Invalid slave address */
    MODBUS_ERR_INVALID_REG,     /* Invalid register address */
    MODBUS_ERR_FRAME,           /* Malformed frame */
    MODBUS_ERR_BUSY,            /* Bus busy */
    MODBUS_ERR_NOT_INIT,        /* Driver not initialized */
} modbus_err_t;

/* Exception Codes (from slave) */
typedef enum {
    MODBUS_EX_ILLEGAL_FUNCTION  = 0x01,
    MODBUS_EX_ILLEGAL_ADDRESS   = 0x02,
    MODBUS_EX_ILLEGAL_VALUE     = 0x03,
    MODBUS_EX_SLAVE_FAILURE     = 0x04,
    MODBUS_EX_ACKNOWLEDGE       = 0x05,
    MODBUS_EX_SLAVE_BUSY        = 0x06,
} modbus_exception_t;

/* Configuration structure */
typedef struct {
    int baud_rate;              /* Baud rate (default 9600) */
    int tx_gpio;                /* TX GPIO (default 17) */
    int rx_gpio;                /* RX GPIO (default 18) */
    int de_gpio;                /* DE/RE GPIO (-1 for auto) */
    int response_timeout_ms;    /* Response timeout (default 100ms) */
} modbus_config_t;

/* Default configuration */
#define MODBUS_CONFIG_DEFAULT() { \
    .baud_rate = MODBUS_DEFAULT_BAUD, \
    .tx_gpio = MODBUS_TX_GPIO, \
    .rx_gpio = MODBUS_RX_GPIO, \
    .de_gpio = MODBUS_DE_GPIO, \
    .response_timeout_ms = MODBUS_RESPONSE_TIMEOUT_MS, \
}

/**
 * @brief Initialize Modbus RTU master
 *
 * Configures UART for RS-485 half-duplex operation.
 *
 * @param config Configuration parameters (NULL for defaults)
 * @return ESP_OK on success
 */
esp_err_t modbus_master_init(const modbus_config_t *config);

/**
 * @brief Deinitialize Modbus master
 */
void modbus_master_deinit(void);

/**
 * @brief Read holding registers (function code 0x03)
 *
 * @param slave_addr Slave address (1-247)
 * @param reg_addr Starting register address (0-based)
 * @param reg_count Number of registers to read (1-125)
 * @param data Output buffer for register values (host byte order)
 * @return modbus_err_t result code
 */
modbus_err_t modbus_read_holding(uint8_t slave_addr, uint16_t reg_addr,
                                  uint16_t reg_count, uint16_t *data);

/**
 * @brief Write single holding register (function code 0x06)
 *
 * @param slave_addr Slave address (1-247)
 * @param reg_addr Register address (0-based)
 * @param value Value to write
 * @return modbus_err_t result code
 */
modbus_err_t modbus_write_single(uint8_t slave_addr, uint16_t reg_addr,
                                  uint16_t value);

/**
 * @brief Write multiple holding registers (function code 0x10)
 *
 * @param slave_addr Slave address (1-247)
 * @param reg_addr Starting register address (0-based)
 * @param reg_count Number of registers to write
 * @param data Register values to write (host byte order)
 * @return modbus_err_t result code
 */
modbus_err_t modbus_write_multiple(uint8_t slave_addr, uint16_t reg_addr,
                                    uint16_t reg_count, const uint16_t *data);

/**
 * @brief Get string description of error code
 */
const char *modbus_err_str(modbus_err_t err);

/**
 * @brief Calculate Modbus CRC-16
 *
 * @param data Data buffer
 * @param len Data length
 * @return CRC-16 value (little-endian as per Modbus RTU)
 */
uint16_t modbus_crc16(const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif
