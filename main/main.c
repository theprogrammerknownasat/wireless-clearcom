/**
 * @file main.c
 * @brief Production Intercom Wireless System - Production Firmware
 *
 * RS-701 compatible wireless intercom system.
 * Supports both base station (AP) and belt pack (STA) modes.
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "config.h"
#include "system/device_manager.h"
#include "system/diagnostics.h"
#include "system/power_manager.h"
#include "system/call_module.h"
#include "audio/audio_codec.h"
#include "audio/audio_opus.h"
#include "audio/audio_processor.h"
#include "audio/audio_tones.h"
#include "network/wifi_manager.h"
#include "network/udp_transport.h"
#include "hardware/gpio_control.h"
#include "hardware/ptt_control.h"
#include "hardware/battery.h"
#include "hardware/volume_control.h"
#include "hardware/clearcom_line.h"

#if TEST_MODE_ENABLE
#if DEVICE_TYPE_BASE
#include "test_mode_base.h"
#elif DEVICE_TYPE_PACK
#include "test_mode_pack.h"
#endif
#endif

static const char *TAG = "MAIN";

//=============================================================================
// CALLBACK HANDLERS
//=============================================================================

static void wifi_event_handler(wifi_event_type_t event, void *data)
{
    switch (event) {
        case WIFI_EVENT_CONNECTED:
            ESP_LOGI(TAG, "WiFi connected");
            device_manager_set_state(DEVICE_STATE_CONNECTED);
            device_manager_update_wifi(true, 0);
            gpio_control_set_led(LED_STATUS, LED_OFF);  // Connected = OK = off
            break;
        case WIFI_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "WiFi disconnected");
            device_manager_set_state(DEVICE_STATE_DISCONNECTED);
            device_manager_update_wifi(false, 0);
            gpio_control_set_led(LED_STATUS, LED_BLINK_FAST);  // Disconnected = fast flash
            break;
        case WIFI_EVENT_GOT_IP:
            ESP_LOGI(TAG, "Got IP address");
            break;
        case WIFI_EVENT_STA_JOINED:
            ESP_LOGI(TAG, "Belt pack connected");
            break;
        case WIFI_EVENT_STA_LEFT:
            ESP_LOGW(TAG, "Belt pack disconnected");
            break;
    }
}

static void udp_rx_handler(const uint8_t *opus_data, uint16_t opus_size,
                           bool remote_ptt_active, bool remote_call_active)
{
    device_manager_packet_received();

#if DEVICE_TYPE_PACK
    power_manager_activity();
#endif

    call_module_remote_signal(remote_call_active);

#if DEVICE_TYPE_BASE
    gpio_control_set_led(LED_PTT_MIRROR, remote_ptt_active ? LED_ON : LED_OFF);
#endif

    int16_t pcm_output[SAMPLES_PER_FRAME];
    int decoded = audio_opus_decode(opus_data, opus_size, pcm_output, SAMPLES_PER_FRAME, 0);

    if (decoded > 0) {
        audio_processor_limit(pcm_output, decoded, LIMITER_THRESHOLD);
        audio_codec_write(pcm_output, decoded);
    }
}

static void ptt_state_handler(ptt_state_t state, bool transmitting)
{
    ESP_LOGI(TAG, "PTT: %s", transmitting ? "ON" : "OFF");
    device_manager_set_ptt_state(state);

#if DEVICE_TYPE_PACK
    gpio_control_set_led(LED_PTT, transmitting ? LED_ON : LED_OFF);

    // Send immediate PTT-OFF packet so base LED doesn't stay stuck on
    if (!transmitting) {
        uint8_t silent_packet[60] = {0};
        bool call_active = call_module_is_calling();
        udp_transport_send(silent_packet, 60, false, call_active);
    }

    if (transmitting) {
        power_manager_activity();
    }
#else
    gpio_control_set_led(LED_PTT_MIRROR, transmitting ? LED_ON : LED_OFF);
#endif
}

static void call_state_handler(call_state_t state, bool is_calling)
{
    const char *state_str =
        state == CALL_IDLE ? "IDLE" :
        state == CALL_OUTGOING ? "CALLING" :
        state == CALL_INCOMING ? "INCOMING" : "ACKNOWLEDGED";

    ESP_LOGI(TAG, "Call: %s", state_str);

    if (state == CALL_OUTGOING) {
        gpio_control_set_led(LED_CALL, LED_BLINK_SLOW);
    } else if (state == CALL_INCOMING || state == CALL_ACKNOWLEDGED) {
        gpio_control_set_led(LED_CALL, LED_ON);
    } else {
        gpio_control_set_led(LED_CALL, LED_OFF);
    }
}

#if DEVICE_TYPE_PACK
static void ptt_button_handler(bool pressed, uint32_t hold_time_ms)
{
    ptt_control_button_event(pressed, hold_time_ms);
    power_manager_activity();
}

static void call_button_handler(bool pressed)
{
    call_module_button_event(pressed);
    power_manager_activity();
}

static void battery_status_handler(float voltage, uint8_t percent,
                                   bool is_low, bool is_critical)
{
    device_manager_update_battery(voltage);

    if (is_critical && TONE_BATTERY_CRITICAL_ENABLE) {
        audio_tones_play(TONE_BATTERY_CRITICAL.frequency_hz,
                        TONE_BATTERY_CRITICAL.duration_ms, 0.3f);
    } else if (is_low && TONE_BATTERY_LOW_ENABLE) {
        audio_tones_play(TONE_BATTERY_LOW.frequency_hz,
                        TONE_BATTERY_LOW.duration_ms, 0.3f);
    }
}

static void power_state_handler(power_state_t state)
{
    ESP_LOGI(TAG, "Power: %s",
             state == POWER_STATE_ACTIVE ? "ACTIVE" :
             state == POWER_STATE_LIGHT_SLEEP ? "LIGHT_SLEEP" : "DEEP_SLEEP");
}
#endif

//=============================================================================
// AUDIO TASK
//=============================================================================

static void audio_task(void *arg)
{
    ESP_LOGI(TAG, "Audio task started");

    int16_t pcm_input[SAMPLES_PER_FRAME];
    uint8_t opus_data[OPUS_MAX_PACKET_SIZE];

    while (1) {
        // I2S read blocks until DMA buffer fills (~20ms at 16kHz)
        // This naturally paces the loop - no additional delay needed
        esp_err_t ret = audio_codec_read(pcm_input, SAMPLES_PER_FRAME, NULL);

        if (ret == ESP_OK && ptt_control_is_transmitting()) {
            int encoded_bytes = audio_opus_encode(pcm_input, SAMPLES_PER_FRAME,
                                                  opus_data, OPUS_MAX_PACKET_SIZE);
            if (encoded_bytes > 0) {
                bool call_active = call_module_is_calling();
                udp_transport_send(opus_data, encoded_bytes, true, call_active);
            }
        }
    }
}

//=============================================================================
// MONITORING TASK
//=============================================================================

static void monitor_task(void *arg)
{
    TickType_t last_wake = xTaskGetTickCount();
    uint32_t stats_counter = 0;

#if DEVICE_TYPE_PACK && PTT_TIMEOUT_ENABLE
    uint32_t ptt_transmit_time = 0;
#endif

    while (1) {
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(1000));
        stats_counter++;

#if DEVICE_TYPE_PACK && PTT_TIMEOUT_ENABLE
        if (ptt_control_is_transmitting()) {
            ptt_transmit_time++;
            if (ptt_transmit_time >= PTT_TIMEOUT_SECONDS) {
                ESP_LOGE(TAG, "PTT timeout (%ds) - forcing idle", PTT_TIMEOUT_SECONDS);
                ptt_control_force_idle();
                ptt_transmit_time = 0;
            }
        } else {
            ptt_transmit_time = 0;
        }
#endif

        if (stats_counter % 5 == 0) {
            if (wifi_manager_is_connected()) {
                int8_t rssi = wifi_manager_get_rssi();
                device_manager_update_wifi(true, rssi);
            }
            device_manager_print_status();

            udp_stats_t stats;
            udp_transport_get_stats(&stats);
            ESP_LOGI(TAG, "Net: TX=%lu RX=%lu Loss=%.1f%%",
                     (unsigned long)stats.packets_sent,
                     (unsigned long)stats.packets_received,
                     stats.packet_loss_percent);

            // Status LED: off=good, slow blink=packet loss, fast blink=disconnected, solid=error
            if (!wifi_manager_is_connected()) {
                gpio_control_set_led(LED_STATUS, LED_BLINK_FAST);
            } else if (stats.packet_loss_percent > PACKET_LOSS_WARN_THRESHOLD) {
                gpio_control_set_led(LED_STATUS, LED_BLINK_SLOW);
            } else {
                gpio_control_set_led(LED_STATUS, LED_OFF);
            }
        }

#if DEVICE_TYPE_PACK
        bool light_sleep, deep_sleep;
        power_manager_check_timeout(&light_sleep, &deep_sleep);

        if (deep_sleep) {
            ESP_LOGW(TAG, "Deep sleep timeout");
            power_manager_enter_deep_sleep();
        } else if (light_sleep) {
            power_manager_enter_light_sleep();
        }
#endif
    }
}

//=============================================================================
// INITIALIZATION
//=============================================================================

static esp_err_t init_subsystems(void)
{
    esp_err_t ret;

    ESP_LOGI(TAG, "Initializing device manager...");
    ret = device_manager_init();
    if (ret != ESP_OK) return ret;

    ESP_LOGI(TAG, "Initializing audio...");
    ret = audio_codec_init();
    if (ret != ESP_OK) return ret;

    ret = audio_opus_init();
    if (ret != ESP_OK) return ret;

    ret = audio_processor_init();
    if (ret != ESP_OK) return ret;

    ret = audio_tones_init();
    if (ret != ESP_OK) return ret;

    ESP_LOGI(TAG, "Initializing network...");
    ret = wifi_manager_init(wifi_event_handler);
    if (ret != ESP_OK) return ret;

    ret = wifi_manager_start();
    if (ret != ESP_OK) return ret;

    ret = udp_transport_init(udp_rx_handler);
    if (ret != ESP_OK) return ret;

    ret = udp_transport_start();
    if (ret != ESP_OK) return ret;

    ESP_LOGI(TAG, "Initializing hardware...");

#if DEVICE_TYPE_PACK
    ret = gpio_control_init(ptt_button_handler, call_button_handler);
#else
    ret = gpio_control_init(NULL, NULL);
#endif
    if (ret != ESP_OK) return ret;

    ret = ptt_control_init(ptt_state_handler);
    if (ret != ESP_OK) return ret;

#if DEVICE_TYPE_PACK
    ret = battery_init(battery_status_handler);
    if (ret != ESP_OK) return ret;
    ret = battery_start();
    if (ret != ESP_OK) return ret;

    // Volume pot shares ADC1 with battery -- must init after battery
    ret = volume_control_init();
    if (ret != ESP_OK) return ret;
    ret = volume_control_start();
    if (ret != ESP_OK) return ret;
#endif

#if DEVICE_TYPE_BASE
    ret = clearcom_line_init();
    if (ret != ESP_OK) return ret;
    ret = clearcom_line_start();
    if (ret != ESP_OK) return ret;

    ret = clearcom_line_call_start();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Call interface failed (audio still functional)");
    }
#endif

    ret = call_module_init(call_state_handler);
    if (ret != ESP_OK) return ret;

#if DEVICE_TYPE_PACK
    ret = power_manager_init(power_state_handler);
    if (ret != ESP_OK) return ret;
#endif

    return ESP_OK;
}

//=============================================================================
// MAIN ENTRY POINT
//=============================================================================

void app_main(void)
{
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  Production Intercom System");
    ESP_LOGI(TAG, "  %s", DEVICE_TYPE_STRING);
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "Firmware: %s (%s %s)", FIRMWARE_VERSION, BUILD_DATE, BUILD_TIME);
    ESP_LOGI(TAG, "Device ID: 0x%02X", DEVICE_ID);
#if DEVICE_TYPE_BASE
    ESP_LOGI(TAG, "Paired Pack: 0x%02X", PAIRED_PACK_ID);
#else
    ESP_LOGI(TAG, "Paired Base: 0x%02X", PAIRED_BASE_ID);
#endif

    diagnostics_print_system_info();

    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    esp_log_level_set("*", LOG_LEVEL);

    // Initialize all subsystems
    ret = init_subsystems();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Init failed! Halting.");
        gpio_control_set_led(LED_STATUS, LED_ON);  // Solid = error
        while (1) { vTaskDelay(pdMS_TO_TICKS(1000)); }
    }

    // Self-test
    diagnostics_result_t diag_results;
    ret = diagnostics_run_self_test(&diag_results);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Self-test failed!");
    }

    // Wait for WiFi
    vTaskDelay(pdMS_TO_TICKS(3000));

    // Configure audio path
#if DEVICE_TYPE_PACK
    audio_codec_set_input(CODEC_INPUT_MIC);
    audio_codec_set_output(CODEC_OUTPUT_SPEAKER);
    audio_codec_set_input_gain(MIC_GAIN_LEVEL);
#else
    audio_codec_set_input(CODEC_INPUT_LINE);
    audio_codec_set_output(CODEC_OUTPUT_LINE);
    audio_codec_set_input_gain(PARTYLINE_INPUT_GAIN);
#endif

    gpio_control_set_led(LED_STATUS, LED_OFF);  // Off = all OK

    // Start tasks
#if TEST_MODE_ENABLE
    // Test mode: skip audio_task to avoid I2S contention
    ESP_LOGW(TAG, "TEST MODE - audio task disabled");
#else
    xTaskCreate(audio_task, "audio", 32768, NULL, 6, NULL);
#endif
    xTaskCreate(monitor_task, "monitor", 4096, NULL, 3, NULL);

    ESP_LOGI(TAG, "System ready");

#if TEST_MODE_ENABLE
    vTaskDelay(pdMS_TO_TICKS(2000));
    test_mode_start();
#endif

    device_manager_set_state(DEVICE_STATE_CONNECTED);
}
