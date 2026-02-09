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
#include "network/wifi_manager.h"
#include "network/udp_transport.h"

static const char *TAG = "MAIN";

//=============================================================================
// TASK DECLARATIONS
//=============================================================================

static void stats_task(void *arg);
static void audio_test_task(void *arg);
static void network_test_task(void *arg);

//=============================================================================
// NETWORK CALLBACKS
//=============================================================================

static void wifi_event_cb(wifi_event_type_t event, void *data)
{
    switch (event) {
        case WIFI_EVENT_CONNECTED:
            ESP_LOGI(TAG, "✓ WiFi connected");
            device_manager_set_state(DEVICE_STATE_CONNECTED);
            break;
        case WIFI_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "✗ WiFi disconnected");
            device_manager_set_state(DEVICE_STATE_DISCONNECTED);
            break;
        case WIFI_EVENT_GOT_IP:
            ESP_LOGI(TAG, "✓ Got IP address");
            break;
        case WIFI_EVENT_STA_JOINED:
            ESP_LOGI(TAG, "✓ Station connected to AP");
            break;
        case WIFI_EVENT_STA_LEFT:
            ESP_LOGW(TAG, "✗ Station disconnected from AP");
            break;
    }
}

static void udp_rx_cb(const uint8_t *opus_data, uint16_t opus_size,
                      bool ptt_active, bool call_active)
{
    ESP_LOGD(TAG, "UDP RX: %u bytes, PTT=%d, Call=%d",
             opus_size, ptt_active, call_active);

    // In full implementation, this would decode and play audio
    // For now, just acknowledge receipt
}

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

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, " PHASE 3: NETWORK SUBSYSTEM");
    ESP_LOGI(TAG, "========================================");

    // Initialize network subsystem
    ESP_LOGI(TAG, "Initializing WiFi manager...");
    ret = wifi_manager_init(wifi_event_cb);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi manager init failed: %d", ret);
    }

    ESP_LOGI(TAG, "Starting WiFi...");
    ret = wifi_manager_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi start failed: %d", ret);
    }

    // Wait for WiFi to connect (give it a few seconds)
    ESP_LOGI(TAG, "Waiting for WiFi connection...");
    vTaskDelay(pdMS_TO_TICKS(3000));

    ESP_LOGI(TAG, "Initializing UDP transport...");
    ret = udp_transport_init(udp_rx_cb);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "UDP transport init failed: %d", ret);
    }

    ESP_LOGI(TAG, "Starting UDP transport...");
    ret = udp_transport_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "UDP transport start failed: %d", ret);
    }

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, " Network subsystem initialized!");
    ESP_LOGI(TAG, "========================================");

    // Set initial audio configuration
    audio_codec_set_input(CODEC_INPUT_MIC);
    audio_codec_set_output(CODEC_OUTPUT_SPEAKER);
    audio_codec_set_input_gain(MIC_GAIN_LEVEL);

    ESP_LOGI(TAG, "Starting test tasks...");

    // Start network test task
    xTaskCreate(network_test_task, "net_test", 8192, NULL, 4, NULL);

    // Start audio test task (commented out for Phase 3 testing)
    // xTaskCreate(audio_test_task, "audio_test", 32768, NULL, 5, NULL);

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
    ESP_LOGI(TAG, "Running audio subsystem tests...");

    int16_t pcm_input[SAMPLES_PER_FRAME];
    int16_t pcm_output[SAMPLES_PER_FRAME];
    uint8_t opus_data[OPUS_MAX_PACKET_SIZE];

    float test_phase = 0.0f;
    uint32_t test_count = 0;
    bool limiter_tested = false;

    while (1) {
        // Run limiter test once at startup
        if (!limiter_tested && test_count == 10) {
            ESP_LOGI(TAG, "========================================");
            ESP_LOGI(TAG, "TEST: Audio Limiter");
            ESP_LOGI(TAG, "========================================");

            // Generate overdriven signal (150% amplitude - should clip without limiter)
            audio_tones_generate_sine(pcm_input, SAMPLES_PER_FRAME, 1000.0f, 1.5f, &test_phase);

            // Check peak before limiting
            int16_t peak_before = 0;
            for (int i = 0; i < SAMPLES_PER_FRAME; i++) {
                if (abs(pcm_input[i]) > peak_before) peak_before = abs(pcm_input[i]);
            }

            // Apply limiter
            audio_processor_limit(pcm_input, SAMPLES_PER_FRAME, LIMITER_THRESHOLD);

            // Check peak after limiting
            int16_t peak_after = 0;
            for (int i = 0; i < SAMPLES_PER_FRAME; i++) {
                if (abs(pcm_input[i]) > peak_after) peak_after = abs(pcm_input[i]);
            }

            int16_t threshold_value = (int16_t)(32767.0f * LIMITER_THRESHOLD);

            ESP_LOGI(TAG, "Before limiter: peak = %d (%.1f%%)", peak_before, (peak_before / 32767.0f) * 100);
            ESP_LOGI(TAG, "After limiter:  peak = %d (%.1f%%)", peak_after, (peak_after / 32767.0f) * 100);
            ESP_LOGI(TAG, "Threshold:      %d (%.1f%%)", threshold_value, LIMITER_THRESHOLD * 100);

            // Limiter uses soft knee, so allow some overshoot
            int16_t acceptable_peak = threshold_value + 1000;  // Allow 3% overshoot for soft limiting

            if (peak_after <= acceptable_peak) {
                ESP_LOGI(TAG, "✓ PASS: Limiter prevented clipping (soft knee limiting)");
            } else {
                ESP_LOGE(TAG, "✗ FAIL: Limiter did not work correctly");
            }

            ESP_LOGI(TAG, "Note: Input clamped at 100%% by int16_t max value");
            ESP_LOGI(TAG, "========================================");
            limiter_tested = true;
            test_phase = 0.0f;  // Reset for normal test
        }

        // Normal Opus encode/decode loopback test
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

            ESP_LOGI(TAG, "Opus loopback: %lu frames, %.2f ms encode, RMS=%.3f, %d bytes",
                     (unsigned long)total_frames, avg_encode_ms, rms, encoded_bytes);
        }

        // Run packet loss test at 100 frames
        if (test_count == 100) {
            ESP_LOGI(TAG, "========================================");
            ESP_LOGI(TAG, "TEST: Opus Packet Loss Concealment");
            ESP_LOGI(TAG, "========================================");

            // Decode with NULL packet (simulates packet loss)
            int plc_samples = audio_opus_decode(NULL, 0, pcm_output, SAMPLES_PER_FRAME, 0);

            if (plc_samples > 0) {
                float plc_rms = audio_processor_get_rms(pcm_output, plc_samples);
                ESP_LOGI(TAG, "✓ PASS: PLC decoded %d samples, RMS=%.3f", plc_samples, plc_rms);
                ESP_LOGI(TAG, "Opus filled gap with concealment audio");
            } else {
                ESP_LOGE(TAG, "✗ FAIL: PLC failed with error %d", plc_samples);
            }

            ESP_LOGI(TAG, "========================================");
        }

        // Delay for frame time
        vTaskDelay(pdMS_TO_TICKS(FRAME_SIZE_MS));
    }
}

//=============================================================================
// NETWORK TEST TASK
//=============================================================================

static void network_test_task(void *arg)
{
    ESP_LOGI(TAG, "Network test task started");
    ESP_LOGI(TAG, "Testing WiFi + UDP audio packet transport...");

    // Wait for WiFi to fully connect
    vTaskDelay(pdMS_TO_TICKS(2000));

    // Test 1: Check WiFi connection
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "TEST: WiFi Connection");
    ESP_LOGI(TAG, "========================================");

    if (wifi_manager_is_connected()) {
        char ip_str[16];
        wifi_manager_get_ip(ip_str);
        int8_t rssi = wifi_manager_get_rssi();

        ESP_LOGI(TAG, "✓ PASS: WiFi connected");
        ESP_LOGI(TAG, "IP Address: %s", ip_str);
#if DEVICE_TYPE_PACK
        ESP_LOGI(TAG, "RSSI: %d dBm", rssi);
#else
        ESP_LOGI(TAG, "Connected stations: %d", wifi_manager_get_sta_count());
#endif
    } else {
        ESP_LOGE(TAG, "✗ FAIL: WiFi not connected");
    }
    ESP_LOGI(TAG, "========================================");

    vTaskDelay(pdMS_TO_TICKS(1000));

    // Test 2: Audio packet transmission
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "TEST: UDP Audio Packet Transport");
    ESP_LOGI(TAG, "========================================");

    uint8_t test_opus_data[60] = {0};  // Fake Opus packet
    uint32_t test_count = 0;

    while (1) {
        // Generate fake Opus data (in real system, this comes from encoder)
        for (int i = 0; i < sizeof(test_opus_data); i++) {
            test_opus_data[i] = (uint8_t)(test_count + i);
        }

        // Simulate PTT and Call states
        bool ptt = (test_count % 100) < 50;  // Toggle every 50 frames
        bool call = (test_count % 200) < 20; // Pulse every 200 frames

        // Send packet
        esp_err_t ret = udp_transport_send(test_opus_data, sizeof(test_opus_data),
                                           ptt, call);

        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to send packet");
        }

        test_count++;

        // Log stats every second
        if (test_count % 50 == 0) {
            udp_stats_t stats;
            udp_transport_get_stats(&stats);

            ESP_LOGI(TAG, "Network: TX=%lu, RX=%lu, Lost=%lu, Loss=%.2f%%",
                     (unsigned long)stats.packets_sent,
                     (unsigned long)stats.packets_received,
                     (unsigned long)stats.packets_lost,
                     stats.packet_loss_percent);

            // Check WiFi quality
            int8_t rssi = wifi_manager_get_rssi();
            if (rssi != 0) {
                ESP_LOGI(TAG, "Signal: %d dBm", rssi);
            }
        }

        // Send at 50 Hz (20ms frame rate)
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}