/**
 * @file test_mode_base.c
 * @brief Base Station Test Mode
 *
 * Test mode that spoofs incoming audio packets and tests party line output.
 * Generates a 440Hz sine wave as if it were received from the belt pack.
 * Also tests call detection from party line.
 *
 * Usage: Set TEST_MODE_ENABLE=1 in config_base.h
 */

#include "test_mode_base.h"
#include "config.h"

#if DEVICE_TYPE_BASE && TEST_MODE_ENABLE

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hal/gpio_types.h"
#include "audio/audio_codec.h"
#include "audio/audio_opus.h"  // ← Correct header!
#include "system/device_manager.h"
#include "esp_adc/adc_oneshot.h"
#include <math.h>

static const char *TAG = "TEST_MODE";

//=============================================================================
// TEST PARAMETERS
//=============================================================================

#define TEST_TONE_FREQ_HZ       440.0f   // A4 note
#define TEST_TONE_AMPLITUDE     0.5f     // 50% volume
#define TEST_PACKET_INTERVAL_MS 20       // 20ms = 50Hz packet rate
#define CALL_CHECK_INTERVAL_MS  100      // Check for calls every 100ms

//=============================================================================
// PRIVATE VARIABLES
//=============================================================================

static bool test_mode_running = false;
static TaskHandle_t test_task_handle = NULL;
static TaskHandle_t call_monitor_task_handle = NULL;
static adc_oneshot_unit_handle_t adc_handle = NULL;

// Sine wave generator state
static float phase = 0.0f;
static const float phase_increment = (2.0f * M_PI * TEST_TONE_FREQ_HZ) / SAMPLE_RATE_HZ;

//=============================================================================
// PRIVATE FUNCTIONS
//=============================================================================

/**
 * @brief Generate one frame of 440Hz sine wave
 */
static void generate_test_audio(int16_t *buffer, size_t sample_count)
{
    for (size_t i = 0; i < sample_count; i++) {
        float sample = sinf(phase) * TEST_TONE_AMPLITUDE;
        buffer[i] = (int16_t)(sample * 32767.0f);

        phase += phase_increment;
        if (phase >= 2.0f * M_PI) {
            phase -= 2.0f * M_PI;
        }
    }
}

/**
 * @brief Test audio packet injection task
 * Simulates receiving Opus packets from belt pack
 */
static void test_audio_task(void *arg)
{
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  BASE STATION TEST MODE ACTIVE");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "Generating 440Hz test tone");
    ESP_LOGI(TAG, "Outputting to party line via WM8960");
    ESP_LOGI(TAG, "========================================");

    int16_t audio_buffer[SAMPLES_PER_FRAME];
    uint8_t encoded_buffer[OPUS_MAX_PACKET_SIZE];

    TickType_t last_wake_time = xTaskGetTickCount();
    uint32_t packet_count = 0;

    while (test_mode_running) {
        // Generate 440Hz sine wave
        generate_test_audio(audio_buffer, SAMPLES_PER_FRAME);

        // Encode with Opus (simulating received packet)
        int encoded_bytes = audio_opus_encode(audio_buffer, SAMPLES_PER_FRAME,
                                              encoded_buffer, sizeof(encoded_buffer));

        if (encoded_bytes > 0) {
            // Decode immediately (simulating packet reception)
            int16_t decoded_buffer[SAMPLES_PER_FRAME];
            int decoded_samples = audio_opus_decode(encoded_buffer, encoded_bytes,
                                                    decoded_buffer, SAMPLES_PER_FRAME, 0);

            if (decoded_samples == SAMPLES_PER_FRAME) {
                // Output to party line via codec
                audio_codec_write(decoded_buffer, SAMPLES_PER_FRAME);

                packet_count++;

                // Log every 50 packets (1 second)
                if (packet_count % 50 == 0) {
                    ESP_LOGI(TAG, "Test packets sent: %lu (%.1fs)",
                            (unsigned long)packet_count, packet_count / 50.0f);
                }
            } else {
                ESP_LOGW(TAG, "Decode failed: %d samples", decoded_samples);
            }
        } else {
            ESP_LOGW(TAG, "Encode failed: %d bytes", encoded_bytes);
        }

        // Wait for next packet interval (20ms)
        vTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(TEST_PACKET_INTERVAL_MS));
    }

    ESP_LOGI(TAG, "Test audio task stopped");
    vTaskDelete(NULL);
}

/**
 * @brief Monitor party line for incoming call signals
 * Reads CALL_RX ADC and detects call button presses
 */
static void call_monitor_task(void *arg)
{
    ESP_LOGI(TAG, "Call monitor task started");
    ESP_LOGI(TAG, "Monitoring GPIO %d for call signals", CALL_RX_PIN);
    ESP_LOGI(TAG, "Threshold: %.2fV", CALL_VOLTAGE_THRESHOLD);

    bool call_active = false;
    uint32_t call_start_time = 0;

    while (test_mode_running) {
        int adc_value = 0;
        esp_err_t ret = adc_oneshot_read(adc_handle, CALL_RX_ADC_CHANNEL, &adc_value);

        if (ret == ESP_OK) {
            // Convert ADC to voltage (12-bit ADC, 3.3V range with attenuation)
            // Adjust this based on your voltage divider circuit
            float voltage = (adc_value / 4095.0f) * 3.3f;

            // Detect call signal
            bool call_detected = (voltage > CALL_VOLTAGE_THRESHOLD);

            if (call_detected && !call_active) {
                // Call started
                call_active = true;
                call_start_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
                ESP_LOGI(TAG, "╔══════════════════════════════╗");
                ESP_LOGI(TAG, "║  CALL SIGNAL DETECTED!       ║");
                ESP_LOGI(TAG, "║  Voltage: %.2fV              ║", voltage);
                ESP_LOGI(TAG, "╚══════════════════════════════╝");

                // Update device manager
                device_manager_set_call_active(true);

            } else if (!call_detected && call_active) {
                // Call ended
                call_active = false;
                uint32_t duration = (xTaskGetTickCount() * portTICK_PERIOD_MS) - call_start_time;
                ESP_LOGI(TAG, "Call ended (duration: %lums)", (unsigned long)duration);

                // Update device manager
                device_manager_set_call_active(false);
            }

            // Log voltage periodically (every 2 seconds) if no call
            static uint32_t last_log_time = 0;
            uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
            if (!call_active && (now - last_log_time > 2000)) {
                ESP_LOGD(TAG, "CALL_RX: %.2fV (ADC: %d)", voltage, adc_value);
                last_log_time = now;
            }
        } else {
            ESP_LOGW(TAG, "ADC read failed: %d", ret);
        }

        vTaskDelay(pdMS_TO_TICKS(CALL_CHECK_INTERVAL_MS));
    }

    ESP_LOGI(TAG, "Call monitor task stopped");
    vTaskDelete(NULL);
}

/**
 * @brief Initialize ADC for call detection
 */
static esp_err_t init_call_adc(void)
{
    // Configure ADC
    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = ADC_UNIT_1,
    };

    esp_err_t ret = adc_oneshot_new_unit(&init_config, &adc_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ADC init failed: %d", ret);
        return ret;
    }

    // Configure channel for CALL_RX
    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH_12,
        .atten = ADC_ATTEN_DB_12,  // 0-3.3V range
    };

    ret = adc_oneshot_config_channel(adc_handle, CALL_RX_ADC_CHANNEL, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ADC channel config failed: %d", ret);
        return ret;
    }

    ESP_LOGI(TAG, "Call detection ADC initialized (GPIO %d, ADC1_CH%d)",
             CALL_RX_PIN, CALL_RX_ADC_CHANNEL);

    return ESP_OK;
}

//=============================================================================
// PUBLIC FUNCTIONS
//=============================================================================

esp_err_t test_mode_start(void)
{
    if (test_mode_running) {
        ESP_LOGW(TAG, "Test mode already running");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Starting base station test mode...");

    // Initialize call detection ADC
    esp_err_t ret = init_call_adc();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize call detection");
        return ret;
    }

    test_mode_running = true;

    // Start test audio generation task
    // NOTE: Opus encoder needs large stack (8KB minimum for encoding)
    xTaskCreate(test_audio_task, "test_audio", 32768, NULL, 5, &test_task_handle);

    // Start call monitoring task
    xTaskCreate(call_monitor_task, "call_monitor", 4096, NULL, 4, &call_monitor_task_handle);

    ESP_LOGI(TAG, "Test mode started successfully");
    ESP_LOGI(TAG, "- Audio: 440Hz sine wave → Party line");
    ESP_LOGI(TAG, "- Call detection: Monitoring GPIO %d", CALL_RX_PIN);

    return ESP_OK;
}

void test_mode_stop(void)
{
    if (!test_mode_running) {
        return;
    }

    ESP_LOGI(TAG, "Stopping test mode...");
    test_mode_running = false;

    // Tasks will self-delete when they see test_mode_running = false
    vTaskDelay(pdMS_TO_TICKS(200));  // Give tasks time to clean up

    // Clean up ADC
    if (adc_handle) {
        adc_oneshot_del_unit(adc_handle);
        adc_handle = NULL;
    }

    ESP_LOGI(TAG, "Test mode stopped");
}

bool test_mode_is_running(void)
{
    return test_mode_running;
}

#endif // DEVICE_TYPE_BASE && TEST_MODE_ENABLE