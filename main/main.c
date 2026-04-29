#include <stdio.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_mcp4922.h"
#include "driver/gptimer.h"

static const char *TAG = "main";

#define NUM_CHIPS 2
#define SINE_POINTS 225

// The LUT for 60Hz sine at 13.5kHz sampling rate. Placed in RAM.
DRAM_ATTR static uint16_t sine_lut[SINE_POINTS];

// Indices for phase shifting
#define PHASE_0_OFFSET   0
#define PHASE_90_OFFSET  56
#define PHASE_180_OFFSET 112

static mcp4922_context_t dac_ctx;
static int lut_idx = 0;

static bool IRAM_ATTR timer_isr_callback(gptimer_handle_t timer, const gptimer_alarm_event_data_t *edata, void *user_data) {
    uint16_t val[4];
    
    val[0] = sine_lut[lut_idx]; // 0 degrees
    val[1] = sine_lut[(lut_idx + PHASE_90_OFFSET) % SINE_POINTS]; // 90 degrees
    val[2] = sine_lut[(lut_idx + PHASE_180_OFFSET) % SINE_POINTS]; // 180 degrees
    val[3] = 0; // Unused channel
    
    mcp4922_write_channels_isr(&dac_ctx, val);
    
    lut_idx++;
    if (lut_idx >= SINE_POINTS) {
        lut_idx = 0;
    }
    
    return false; // Don't yield
}

static void gptimer_init_task(void *arg) {
    ESP_LOGI(TAG, "Initializing GPTimer on Core %d", xPortGetCoreID());

    gptimer_handle_t gptimer = NULL;
    gptimer_config_t timer_config = {
        .clk_src = GPTIMER_CLK_SRC_DEFAULT,
        .direction = GPTIMER_COUNT_UP,
        .resolution_hz = 40000000, // 40MHz para altíssima precisão
    };
    
    ESP_ERROR_CHECK(gptimer_new_timer(&timer_config, &gptimer));

    gptimer_event_callbacks_t cbs = {
        .on_alarm = timer_isr_callback,
    };
    ESP_ERROR_CHECK(gptimer_register_event_callbacks(gptimer, &cbs, NULL));

    gptimer_alarm_config_t alarm_config = {
        .reload_count = 0,
        .alarm_count = 2963, // 40,000,000 / 13500 = 2962.96 -> 2963
        .flags.auto_reload_on_alarm = true,
    };
    ESP_ERROR_CHECK(gptimer_set_alarm_action(gptimer, &alarm_config));

    ESP_ERROR_CHECK(gptimer_enable(gptimer));
    ESP_ERROR_CHECK(gptimer_start(gptimer));

    ESP_LOGI(TAG, "GPTimer started at 13.5kHz");
    
    // Once configured, the task can be deleted. The timer ISR will remain on this core.
    vTaskDelete(NULL);
}

void app_main(void) {
    ESP_LOGI(TAG, "Starting MCP4922 Sine Wave Generator Example");

    // Populate the LUT
    for (int i = 0; i < SINE_POINTS; i++) {
        // Sine wave centered at 2048, amplitude 2047 (0 to 4095)
        double angle = (2.0 * M_PI * i) / SINE_POINTS;
        sine_lut[i] = (uint16_t)(2048.0 + 2047.0 * sin(angle));
    }

    // Configure MCP4922
    gpio_num_t cs_pins[NUM_CHIPS] = {
        CONFIG_MCP4922_CS1_PIN,
        CONFIG_MCP4922_CS2_PIN
    };

    mcp4922_config_t dac_config = {
        .host_id = SPI2_HOST,
        .mosi_io_num = CONFIG_MCP4922_MOSI_PIN,
        .sck_io_num = CONFIG_MCP4922_SCK_PIN,
        .ldac_io_num = CONFIG_MCP4922_LDAC_PIN,
        .num_chips = NUM_CHIPS,
        .cs_pins = cs_pins,
        .gain_2x = false,
        .vref_buffered = true
    };

    esp_err_t ret = mcp4922_init(&dac_config, &dac_ctx);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize MCP4922");
        return;
    }

    // Create a task pinned to Core 1 to initialize the GPTimer.
    // This ensures that the GPTimer ISR runs on Core 1, away from Wi-Fi/BT on Core 0.
    xTaskCreatePinnedToCore(gptimer_init_task, "gptimer_init", 4096, NULL, 5, NULL, 1);
}
