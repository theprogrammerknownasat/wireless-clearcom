/**
 * @file clearcom_line.c
 * @brief Party Line Interface Implementation
 */

#include "clearcom_line.h"
#include "../config.h"

#if DEVICE_TYPE_BASE

#include "../audio/audio_codec.h"
#include "../audio/audio_processor.h"
#include "../system/call_module.h"
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "CC_LINE";

//=============================================================================
// PRIVATE VARIABLES
//=============================================================================

static bool initialized = false;
static bool running = false;
static clearcom_line_status_t status = {0};

// Call monitoring
static adc_oneshot_unit_handle_t call_adc_handle = NULL;
static bool call_monitor_running = false;
static TaskHandle_t call_monitor_handle = NULL;
static bool call_tx_active = false;  // True when WE are asserting call

//=============================================================================
// PRIVATE FUNCTIONS
//=============================================================================

static bool detect_dc_offset(const int16_t *buffer, size_t sample_count)
{
    // Calculate DC offset (average of signal)
    int32_t sum = 0;
    for (size_t i = 0; i < sample_count; i++) {
        sum += buffer[i];
    }

    int16_t dc_offset = sum / sample_count;

    // If DC offset is significant (>10% of max), flag as fault
    const int16_t dc_threshold = 3276;  // 10% of 32767

    return (abs(dc_offset) > dc_threshold);
}

//=============================================================================
// PUBLIC FUNCTIONS
//=============================================================================

esp_err_t clearcom_line_init(void)
{
    if (initialized) {
        ESP_LOGW(TAG, "Intercom line already initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing party line interface...");

    // Line interface uses WM8960 line input/output
    // Already initialized by audio_codec_init()

    // Set initial gains
    clearcom_line_set_input_gain(PARTYLINE_INPUT_GAIN);
    clearcom_line_set_output_gain(PARTYLINE_OUTPUT_GAIN);

    // Reset status
    memset(&status, 0, sizeof(status));
    status.line_connected = true;  // Assume connected (no detection circuit)

    initialized = true;
    ESP_LOGI(TAG, "Intercom line interface initialized");

    return ESP_OK;
}

esp_err_t clearcom_line_start(void)
{
    if (!initialized) {
        ESP_LOGE(TAG, "Intercom line not initialized");
        return ESP_FAIL;
    }

    if (running) {
        ESP_LOGW(TAG, "Intercom line already running");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Starting intercom line interface...");

    // Switch codec to line input/output mode
    audio_codec_set_input(CODEC_INPUT_LINE);
    audio_codec_set_output(CODEC_OUTPUT_LINE);

    running = true;
    ESP_LOGI(TAG, "Intercom line interface started");

    return ESP_OK;
}

esp_err_t clearcom_line_stop(void)
{
    if (!running) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Stopping intercom line interface...");

    running = false;

    return ESP_OK;
}

esp_err_t clearcom_line_read(int16_t *buffer, size_t sample_count)
{
    if (!initialized || !running || !buffer) {
        return ESP_FAIL;
    }

    // Read from codec line input
    esp_err_t ret = audio_codec_read(buffer, sample_count, NULL);

    if (ret == ESP_OK) {
        // Update status
        status.input_level = audio_processor_get_rms(buffer, sample_count);

        // Check for DC offset (fault condition)
        if (PARTYLINE_DC_BLOCKING) {
            status.dc_offset_detected = detect_dc_offset(buffer, sample_count);
            if (status.dc_offset_detected) {
                ESP_LOGW(TAG, "DC offset detected on party line input");
            }
        }
    }

    return ret;
}

esp_err_t clearcom_line_write(const int16_t *buffer, size_t sample_count)
{
    if (!initialized || !running || !buffer) {
        return ESP_FAIL;
    }

    // Update output level
    status.output_level = audio_processor_get_rms(buffer, sample_count);

    // Write to codec line output
    return audio_codec_write(buffer, sample_count);
}

void clearcom_line_get_status(clearcom_line_status_t *status_out)
{
    if (status_out) {
        memcpy(status_out, &status, sizeof(clearcom_line_status_t));
    }
}

void clearcom_line_set_output_gain(uint8_t gain)
{
    if (gain > 31) gain = 31;
    audio_codec_set_output_volume(gain);
    ESP_LOGI(TAG, "Party line output gain: %d", gain);
}

void clearcom_line_set_input_gain(uint8_t gain)
{
    if (gain > 31) gain = 31;
    audio_codec_set_input_gain(gain);
    ESP_LOGI(TAG, "Party line input gain: %d", gain);
}

//=============================================================================
// CALL MONITORING
//=============================================================================

/**
 * @brief Partyline call monitoring task
 * - Reads ADC to detect incoming calls from partyline
 * - Drives MOSFET to assert calls onto partyline (when pack calls)
 */
static void call_monitor_task(void *arg)
{
    ESP_LOGI(TAG, "Call monitor started (RX: GPIO%d, TX: GPIO%d)",
             CALL_RX_PIN, CALL_TX_PIN);

    bool partyline_call_active = false;
    uint32_t debounce_count = 0;
    const uint32_t debounce_threshold = CALL_DEBOUNCE_MS / 50;  // 50ms poll interval

    while (call_monitor_running) {
        // === CALL TX: Drive MOSFET based on pack's call state ===
        // When pack is calling (base state = INCOMING or ACKNOWLEDGED),
        // assert call on partyline
        bool pack_calling = call_module_is_being_called();
        if (pack_calling != call_tx_active) {
            call_tx_active = pack_calling;
            gpio_set_level(CALL_TX_PIN, call_tx_active ? 1 : 0);
            ESP_LOGI(TAG, "Call TX %s (relaying pack call to partyline)",
                     call_tx_active ? "ASSERTED" : "RELEASED");
        }

        // === CALL RX: Read partyline call voltage ===
        // Skip reading when we're asserting call (avoid self-detection)
        if (!call_tx_active) {
            int adc_value = 0;
            esp_err_t ret = adc_oneshot_read(call_adc_handle, CALL_RX_ADC_CHANNEL, &adc_value);

            if (ret == ESP_OK) {
                float voltage = (adc_value / 4095.0f) * 3.3f;
                bool call_detected = (voltage > CALL_VOLTAGE_THRESHOLD);

                if (call_detected && !partyline_call_active) {
                    debounce_count++;
                    if (debounce_count >= debounce_threshold) {
                        partyline_call_active = true;
                        debounce_count = 0;
                        ESP_LOGI(TAG, "Partyline CALL detected (%.2fV)", voltage);
                        // Signal to call_module as if local button pressed
                        // This relays the partyline call to the pack via UDP
                        call_module_button_event(true);
                    }
                } else if (!call_detected && partyline_call_active) {
                    debounce_count++;
                    if (debounce_count >= debounce_threshold) {
                        partyline_call_active = false;
                        debounce_count = 0;
                        ESP_LOGI(TAG, "Partyline call ended (%.2fV)", voltage);
                        call_module_button_event(false);
                    }
                } else {
                    debounce_count = 0;
                }
            }
        } else {
            // While TX is active, clear RX state to avoid stale detection
            partyline_call_active = false;
            debounce_count = 0;
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }

    // Clean up: ensure MOSFET is off
    gpio_set_level(CALL_TX_PIN, 0);
    ESP_LOGI(TAG, "Call monitor stopped");
    vTaskDelete(NULL);
}

esp_err_t clearcom_line_call_start(void)
{
    if (call_monitor_running) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Starting partyline call interface...");

    // Initialize call TX GPIO (drives 2N7002K MOSFET)
    gpio_config_t tx_conf = {
        .pin_bit_mask = (1ULL << CALL_TX_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t ret = gpio_config(&tx_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure call TX GPIO%d", CALL_TX_PIN);
        return ret;
    }
    gpio_set_level(CALL_TX_PIN, 0);  // MOSFET off
    ESP_LOGI(TAG, "Call TX GPIO%d configured (MOSFET driver)", CALL_TX_PIN);

    // Initialize call RX ADC
    adc_oneshot_unit_init_cfg_t adc_cfg = {
        .unit_id = ADC_UNIT_1,
    };
    ret = adc_oneshot_new_unit(&adc_cfg, &call_adc_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init call RX ADC");
        return ret;
    }

    adc_oneshot_chan_cfg_t chan_cfg = {
        .bitwidth = ADC_BITWIDTH_12,
        .atten = ADC_ATTEN_DB_12,  // 0-3.3V range
    };
    ret = adc_oneshot_config_channel(call_adc_handle, CALL_RX_ADC_CHANNEL, &chan_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure call RX ADC channel");
        adc_oneshot_del_unit(call_adc_handle);
        call_adc_handle = NULL;
        return ret;
    }
    ESP_LOGI(TAG, "Call RX ADC configured (GPIO%d, threshold %.1fV)",
             CALL_RX_PIN, CALL_VOLTAGE_THRESHOLD);

    // Start monitoring task
    call_monitor_running = true;
    xTaskCreate(call_monitor_task, "call_mon", 3072, NULL, 4, &call_monitor_handle);

    ESP_LOGI(TAG, "Partyline call interface started");
    return ESP_OK;
}

void clearcom_line_call_stop(void)
{
    if (!call_monitor_running) {
        return;
    }

    call_monitor_running = false;
    vTaskDelay(pdMS_TO_TICKS(100));  // Let task exit

    if (call_adc_handle) {
        adc_oneshot_del_unit(call_adc_handle);
        call_adc_handle = NULL;
    }

    gpio_set_level(CALL_TX_PIN, 0);  // Ensure MOSFET off
    ESP_LOGI(TAG, "Partyline call interface stopped");
}

void clearcom_line_deinit(void)
{
    if (!initialized) {
        return;
    }

    ESP_LOGI(TAG, "Deinitializing intercom line interface...");

    clearcom_line_call_stop();
    clearcom_line_stop();

    initialized = false;
    ESP_LOGI(TAG, "Intercom line interface deinitialized");
}

#endif // DEVICE_TYPE_BASE
