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

// Indices for phase shifting (3-phase network)
#define PHASE_0_OFFSET   0
#define PHASE_120_OFFSET 75   // 120 degrees = 1/3 of 225 points
#define PHASE_240_OFFSET 150  // 240 degrees = 2/3 of 225 points

static mcp4922_context_t dac_ctx;
static int lut_idx = 0;

static TaskHandle_t dac_task_handle = NULL;

/* 
 * ============================================================================
 * CRITICAL ARCHITECTURE WARNING (ISR vs FPU)
 * ============================================================================
 * Do NOT perform floating-point calculations (float/double) inside this ISR!
 * 
 * On the ESP32 (Xtensa architecture), the C compiler (GCC) automatically 
 * translates ANY `float` operation into hardware FPU instructions (e.g., mul.s).
 * To minimize interrupt latency, the ESP-IDF FreeRTOS port DOES NOT save the 
 * FPU register states (f0-f15) when entering an ISR.
 * 
 * If this ISR interrupts a Task that was in the middle of a floating-point 
 * calculation, and you do a `float` operation here, the FPU registers will be 
 * overwritten ("dirtied"). When the ISR returns, the original Task will read 
 * corrupted math data, leading to catastrophic and hard-to-debug system crashes.
 * 
 * GOLDEN RULE: The ISR must only clear flags and wake up a High-Priority Task 
 * (Deferred Interrupt Pattern). All PI/PID control loops and heavy math MUST 
 * be executed inside `dac_writer_task`.
 * ============================================================================
 */
static bool IRAM_ATTR timer_isr_callback(gptimer_handle_t timer, const gptimer_alarm_event_data_t *edata, void *user_data) {
    BaseType_t high_task_awoken = pdFALSE;
    vTaskNotifyGiveFromISR(dac_task_handle, &high_task_awoken);
    return (high_task_awoken == pdTRUE);
}

static void dac_writer_task(void *arg) {
    ESP_LOGI(TAG, "Initializing GPTimer on Core %d", xPortGetCoreID());

    gptimer_handle_t gptimer = NULL;
    gptimer_config_t timer_config = {
        .clk_src = GPTIMER_CLK_SRC_DEFAULT,
        .direction = GPTIMER_COUNT_UP,
        .resolution_hz = 40000000, // 40MHz
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

    ESP_LOGI(TAG, "GPTimer started at 13.5kHz. Entering DAC write loop.");
    
    // Acquire the SPI bus permanently for this task.
    // This removes the overhead of acquiring the FreeRTOS mutex on every single transmission.
    spi_device_acquire_bus(dac_ctx.spi_handle, portMAX_DELAY);
    
    uint16_t val[4] = {0, 0, 0, 0};
    
    // Send a dummy transaction using the high-level API to properly configure the SPI hardware
    // registers (clock speed, mode, bit length = 16) inside the ESP32.
    mcp4922_write_channels(&dac_ctx, val);
    
    while (1) {
        // Wait for notification from ISR
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        
        val[0] = sine_lut[lut_idx]; // Fase R (0 graus)
        val[1] = sine_lut[(lut_idx + PHASE_120_OFFSET) % SINE_POINTS]; // Fase S (120 graus)
        val[2] = sine_lut[(lut_idx + PHASE_240_OFFSET) % SINE_POINTS]; // Fase T (240 graus)
        val[3] = 0; // Unused channel
        
        mcp4922_ll_write_channels(&dac_ctx, val);
        
        lut_idx++;
        if (lut_idx >= SINE_POINTS) {
            lut_idx = 0;
        }
    }
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

    // Create a high-priority task pinned to Core 1 to handle SPI transmissions.
    // This ensures that the GPTimer ISR runs on Core 1, and the SPI task is immediately awoken.
    xTaskCreatePinnedToCore(dac_writer_task, "dac_writer", 4096, NULL, configMAX_PRIORITIES - 1, &dac_task_handle, 1);
}
