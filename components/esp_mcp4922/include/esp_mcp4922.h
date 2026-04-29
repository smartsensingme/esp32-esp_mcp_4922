#pragma once

#include "esp_err.h"
#include "esp_attr.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief MCP4922 configuration structure
 */
typedef struct {
    spi_host_device_t host_id;  /*!< SPI host device (e.g., SPI2_HOST, SPI3_HOST) */
    int mosi_io_num;            /*!< GPIO pin for MOSI */
    int sck_io_num;             /*!< GPIO pin for SCK */
    int ldac_io_num;            /*!< GPIO pin for LDAC */
    int num_chips;              /*!< Number of MCP4922 chips connected on the bus */
    gpio_num_t *cs_pins;        /*!< Array of CS GPIO pins, length must match num_chips */
    bool gain_2x;               /*!< True for 2x gain, false for 1x gain */
    bool vref_buffered;         /*!< True for buffered VREF, false for unbuffered */
} mcp4922_config_t;

/**
 * @brief MCP4922 context structure
 */
typedef struct {
    spi_device_handle_t *spi_handles; /*!< Array of SPI device handles, one for each chip */
    int num_chips;                    /*!< Number of chips initialized */
    int ldac_io_num;                  /*!< LDAC GPIO pin, stored for pulsing */
    uint16_t config_bits_a;           /*!< Pre-computed config bits for channel A */
    uint16_t config_bits_b;           /*!< Pre-computed config bits for channel B */
} mcp4922_context_t;

/**
 * @brief Initialize the MCP4922 devices
 * 
 * @param config Pointer to the configuration structure
 * @param ctx Pointer to the context structure to be filled
 * @return ESP_OK on success, otherwise an error code
 */
esp_err_t mcp4922_init(const mcp4922_config_t *config, mcp4922_context_t *ctx);

/**
 * @brief Write values to all channels (Normal Task context)
 * 
 * @param ctx Pointer to the context structure
 * @param channel_values Array of values to write. Length should be ctx->num_chips * 2. 
 *                       Values should be 12-bit (0-4095).
 * @return ESP_OK on success, otherwise an error code
 */
esp_err_t mcp4922_write_channels(mcp4922_context_t *ctx, uint16_t *channel_values);

/**
 * @brief Write values to all channels (ISR context)
 * 
 * @note This function is placed in IRAM and uses polling for minimum latency.
 *       It must ONLY be called from hardware interrupts.
 * 
 * @param ctx Pointer to the context structure
 * @param channel_values Array of values to write. Length should be ctx->num_chips * 2. 
 *                       Values should be 12-bit (0-4095).
 */
void mcp4922_write_channels_isr(mcp4922_context_t *ctx, uint16_t *channel_values);

#ifdef __cplusplus
}
#endif
