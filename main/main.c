/**
 * @file main.c
 * @brief ClearCom Wireless System - Main Entry Point
 *
 * Production firmware for wireless ClearCom RS-701 emulation.
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"

#include "config.h"
#include "system/device_manager.h"
#include "audio/audio_codec.h"
#include "audio/audio_opus.h"
#include "audio/audio_processor.h"
#include "audio/audio_tones.h"

static const char *TAG = "MAIN";

//=============================================================================
// TASK DECLARATIONS
//=============================================================================

static void stats_task(void *arg);
static void audio_test_task(void *arg);

//=============================================================================
// MAIN APPLICATION
//=============================================================================

void app_main(void)
{
    esp_err_t ret;

    // Print banner
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  ClearCom Wireless System");
    ESP_LOGI(TAG, "  %s", DEVICE_TYPE_STRING);
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "Firmware Version: %s", FIRMWARE_VERSION);
    ESP_LOGI(TAG, "Build Date: %s %s", BUILD_DATE, BUILD_TIME);
    ESP_LOGI(TAG, "Device ID: 0x%02X", DEVICE_ID);
#if DEVICE_TYPE_BASE
    ESP_LOGI(TAG, "Paired Pack: 0x%02X", PAIRED_PACK_ID);
#else
    ESP_LOGI(TAG, "Paired Base: 0x%02X", PAIRED_BASE_ID);
#endif
    ESP_LOGI(TAG, "========================================");

    // Initialize NVS (required for WiFi)
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS flash needs erasing, erasing now...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "NVS initialized");

    // Initialize device manager
    ret = device_manager_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize device manager: %d", ret);
        return;
    }

    // Set log level from config
    esp_log_level_set("*", LOG_LEVEL);

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, " PHASE 2: AUDIO SUBSYSTEM TEST");
    ESP_LOGI(TAG, "========================================");

    // Initialize audio subsystem
    ESP_LOGI(TAG, "Initializing audio codec...");
    ret = audio_codec_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Audio codec init failed: %d", ret);
    }

    ESP_LOGI(TAG, "Initializing Opus codec...");
    ret = audio_opus_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Opus init failed: %d", ret);
    }

    ESP_LOGI(TAG, "Initializing audio processor...");
    ret = audio_processor_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Audio processor init failed: %d", ret);
    }

    ESP_LOGI(TAG, "Initializing tone generator...");
    ret = audio_tones_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Tone generator init failed: %d", ret);
    }

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, " Audio subsystem initialized!");
    ESP_LOGI(TAG, "========================================");

    // Set initial audio configuration
    audio_codec_set_input(CODEC_INPUT_MIC);
    audio_codec_set_output(CODEC_OUTPUT_SPEAKER);
    audio_codec_set_input_gain(MIC_GAIN_LEVEL);

    ESP_LOGI(TAG, "Starting test tasks...");

    // Start audio test task
    xTaskCreate(audio_test_task, "audio_test", 8192, NULL, 5, NULL);

    // Device manager is initialized and ready
    device_manager_set_state(DEVICE_STATE_INIT);

    ESP_LOGI(TAG, "System initialized, entering main loop");
    ESP_LOGI(TAG, "========================================");

    // Main loop placeholder
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));

        // Update device state (placeholder)
        device_info_t *info = device_manager_get_info();
        if (info) {
            // Simulate battery drain (pack only, for testing)
#if DEVICE_TYPE_PACK
            static float test_voltage = BATTERY_FULL_VOLTAGE;
            test_voltage -= 0.001f; // Slow drain
            if (test_voltage < BATTERY_EMPTY_VOLTAGE) {
                test_voltage = BATTERY_FULL_VOLTAGE; // Reset for testing
            }
            device_manager_update_battery(test_voltage);
#endif
        }
    }
}

//=============================================================================
// STATS TASK
//=============================================================================

static void stats_task(void *arg)
{
    TickType_t last_wake = xTaskGetTickCount();

    ESP_LOGI(TAG, "Stats task started (interval: %d ms)", STATS_INTERVAL_MS);

    while (1) {
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(STATS_INTERVAL_MS));

        // Print device status
        device_manager_print_status();

        // Check for sleep condition (pack only)
        if (device_manager_should_sleep()) {
            ESP_LOGW(TAG, "Sleep timeout reached (not implemented yet)");
        }
    }
}

//=============================================================================
// AUDIO TEST TASK
//=============================================================================

static void audio_test_task(void *arg)
{
    ESP_LOGI(TAG, "Audio test task started");
    ESP_LOGI(TAG, "Testing Opus encode/decode loopback...");

    int16_t pcm_input[SAMPLES_PER_FRAME];
    int16_t pcm_output[SAMPLES_PER_FRAME];
    uint8_t opus_data[OPUS_MAX_PACKET_SIZE];

    float test_phase = 0.0f;
    uint32_t test_count = 0;

    while (1) {
        // Generate test audio (440Hz sine wave)
        audio_tones_generate_sine(pcm_input, SAMPLES_PER_FRAME, 440.0f, 0.5f, &test_phase);

        // Encode to Opus
        int encoded_bytes = audio_opus_encode(pcm_input, SAMPLES_PER_FRAME,
                                              opus_data, OPUS_MAX_PACKET_SIZE);

        if (encoded_bytes < 0) {
            ESP_LOGE(TAG, "Encode failed: %d", encoded_bytes);
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        // Decode from Opus
        int decoded_samples = audio_opus_decode(opus_data, encoded_bytes,
                                                pcm_output, SAMPLES_PER_FRAME, 0);

        if (decoded_samples < 0) {
            ESP_LOGE(TAG, "Decode failed: %d", decoded_samples);
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        // Apply limiter
        audio_processor_limit(pcm_output, decoded_samples, LIMITER_THRESHOLD);

        // Calculate RMS level
        float rms = audio_processor_get_rms(pcm_output, decoded_samples);

        test_count++;

        if (test_count % 50 == 0) {  // Log every 50 frames (every 1 second at 20ms frames)
            float avg_encode_ms;
            uint32_t total_frames;
            audio_opus_get_stats(&avg_encode_ms, &total_frames);

            ESP_LOGI(TAG, "Opus test: %lu frames, %.2f ms avg encode, RMS=%.3f, %d bytes",
                     (unsigned long)total_frames, avg_encode_ms, rms, encoded_bytes);
        }

        // Delay for frame time
        vTaskDelay(pdMS_TO_TICKS(FRAME_SIZE_MS));
    }
}