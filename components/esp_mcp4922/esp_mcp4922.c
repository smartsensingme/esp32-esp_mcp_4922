#include "esp_mcp4922.h"
#include "esp_log.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hal/gpio_hal.h"
#include "rom/ets_sys.h"
#include <string.h>

static const char *TAG = "MCP4922";

#define MCP4922_DAC_A_BIT (0 << 15)
#define MCP4922_DAC_B_BIT (1 << 15)
#define MCP4922_BUF_BIT   (1 << 14)
#define MCP4922_GAIN_1X   (1 << 13)
#define MCP4922_GAIN_2X   (0 << 13)
#define MCP4922_SHDN_BIT  (1 << 12)

esp_err_t mcp4922_init(const mcp4922_config_t *config, mcp4922_context_t *ctx) {
    ESP_RETURN_ON_FALSE(config != NULL, ESP_ERR_INVALID_ARG, TAG, "Config must not be NULL");
    ESP_RETURN_ON_FALSE(ctx != NULL, ESP_ERR_INVALID_ARG, TAG, "Context must not be NULL");
    ESP_RETURN_ON_FALSE(config->num_chips > 0, ESP_ERR_INVALID_ARG, TAG, "num_chips must be > 0");
    ESP_RETURN_ON_FALSE(config->cs_pins != NULL, ESP_ERR_INVALID_ARG, TAG, "cs_pins array must not be NULL");

    // Initialize SPI bus
    spi_bus_config_t buscfg = {
        .miso_io_num = -1, // Not used
        .mosi_io_num = config->mosi_io_num,
        .sclk_io_num = config->sck_io_num,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = config->num_chips * 4 // Max transfer size needed for 2 channels per chip (16 bit each = 2 bytes * 2 = 4 bytes per chip)
    };

    // We only initialize the bus if it's not already initialized.
    // In ESP-IDF, if you call spi_bus_initialize on an already init'd bus, it returns ESP_ERR_INVALID_STATE.
    // We'll try to init, and ignore INVALID_STATE.
    esp_err_t ret = spi_bus_initialize(config->host_id, &buscfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Failed to initialize SPI bus: %s", esp_err_to_name(ret));
        return ret;
    }

    // Allocate array for SPI handles
    ctx->spi_handles = malloc(config->num_chips * sizeof(spi_device_handle_t));
    ESP_RETURN_ON_FALSE(ctx->spi_handles != NULL, ESP_ERR_NO_MEM, TAG, "Failed to allocate memory for SPI handles");
    ctx->num_chips = config->num_chips;
    ctx->ldac_io_num = config->ldac_io_num;

    // Pre-calculate configuration bits
    uint16_t base_config = MCP4922_SHDN_BIT; // Active mode
    if (config->vref_buffered) {
        base_config |= MCP4922_BUF_BIT;
    }
    if (!config->gain_2x) {
        base_config |= MCP4922_GAIN_1X;
    } else {
        base_config |= MCP4922_GAIN_2X;
    }

    ctx->config_bits_a = base_config | MCP4922_DAC_A_BIT;
    ctx->config_bits_b = base_config | MCP4922_DAC_B_BIT;

    // Add devices
    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 20 * 1000 * 1000, // 20 MHz max as per MCP4922 datasheet
        .mode = 0,                          // SPI mode 0 (CPOL=0, CPHA=0)
        .spics_io_num = -1,                 // We will set this in the loop
        .queue_size = 2,
        .flags = SPI_DEVICE_HALFDUPLEX,
    };

    for (int i = 0; i < config->num_chips; i++) {
        devcfg.spics_io_num = config->cs_pins[i];
        ret = spi_bus_add_device(config->host_id, &devcfg, &ctx->spi_handles[i]);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to add SPI device for CS %d: %s", config->cs_pins[i], esp_err_to_name(ret));
            // Cleanup on failure
            for(int j=0; j<i; j++) spi_bus_remove_device(ctx->spi_handles[j]);
            free(ctx->spi_handles);
            return ret;
        }
    }

    // Configure LDAC pin
    if (config->ldac_io_num >= 0) {
        gpio_config_t io_conf = {
            .intr_type = GPIO_INTR_DISABLE,
            .mode = GPIO_MODE_OUTPUT,
            .pin_bit_mask = (1ULL << config->ldac_io_num),
            .pull_down_en = 0,
            .pull_up_en = 0
        };
        gpio_config(&io_conf);
        gpio_set_level(config->ldac_io_num, 1); // LDAC is active low, keep high by default
    }

    return ESP_OK;
}

esp_err_t mcp4922_write_channels(mcp4922_context_t *ctx, uint16_t *channel_values) {
    if (!ctx || !channel_values) return ESP_ERR_INVALID_ARG;

    for (int i = 0; i < ctx->num_chips; i++) {
        uint16_t val_a = channel_values[i * 2] & 0x0FFF;
        uint16_t val_b = channel_values[i * 2 + 1] & 0x0FFF;

        uint16_t data_a = ctx->config_bits_a | val_a;
        uint16_t data_b = ctx->config_bits_b | val_b;

        // Swap bytes because SPI transmits MSB first, and our ESP is Little Endian
        data_a = __builtin_bswap16(data_a);
        data_b = __builtin_bswap16(data_b);

        spi_transaction_t t_a = {
            .length = 16,
            .tx_buffer = &data_a
        };
        
        spi_transaction_t t_b = {
            .length = 16,
            .tx_buffer = &data_b
        };

        // Transmit Channel A
        esp_err_t ret = spi_device_transmit(ctx->spi_handles[i], &t_a);
        if (ret != ESP_OK) return ret;

        // Transmit Channel B
        ret = spi_device_transmit(ctx->spi_handles[i], &t_b);
        if (ret != ESP_OK) return ret;
    }

    // Latch data (LDAC pulse)
    if (ctx->ldac_io_num >= 0) {
        gpio_set_level(ctx->ldac_io_num, 0);
        ets_delay_us(1); // Small delay to satisfy LDAC pulse width req (min 100ns)
        gpio_set_level(ctx->ldac_io_num, 1);
    }

    return ESP_OK;
}

void IRAM_ATTR mcp4922_write_channels_isr(mcp4922_context_t *ctx, uint16_t *channel_values) {
    // Note: No validity checks here to maximize speed in ISR.

    for (int i = 0; i < ctx->num_chips; i++) {
        uint16_t val_a = channel_values[i * 2] & 0x0FFF;
        uint16_t val_b = channel_values[i * 2 + 1] & 0x0FFF;

        uint16_t data_a = ctx->config_bits_a | val_a;
        uint16_t data_b = ctx->config_bits_b | val_b;

        data_a = __builtin_bswap16(data_a);
        data_b = __builtin_bswap16(data_b);

        spi_transaction_t t_a = {
            .length = 16,
            .tx_buffer = &data_a
        };
        
        spi_transaction_t t_b = {
            .length = 16,
            .tx_buffer = &data_b
        };

        // Use polling transmit for ISR context
        spi_device_polling_transmit(ctx->spi_handles[i], &t_a);
        spi_device_polling_transmit(ctx->spi_handles[i], &t_b);
    }

    // Latch data (LDAC pulse)
    if (ctx->ldac_io_num >= 0) {
        // Fast GPIO clear/set using HAL
        gpio_ll_set_level(&GPIO, ctx->ldac_io_num, 0);
        // Usually, few NOPs are enough for 100ns at 240MHz (1 cycle = ~4ns. 25 cycles = 100ns)
        for(int n=0; n<10; n++) asm volatile("nop");
        gpio_ll_set_level(&GPIO, ctx->ldac_io_num, 1);
    }
}
