/**
 * @file main.c
 * @brief ClearCom Wireless System - Production Firmware
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
#include "esp_timer.h"
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
#include "hardware/clearcom_line.h"
#if DEVICE_TYPE_BASE && TEST_MODE_ENABLE
#include "test_mode_base.h"
# elif DEVICE_TYPE_PACK && TEST_MODE_ENABLE
#include "test_mode_pack.h"
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
            device_manager_update_wifi(true, 0);  // RSSI will be polled by monitor task
            break;
            
        case WIFI_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "WiFi disconnected - reconnecting...");
            device_manager_set_state(DEVICE_STATE_DISCONNECTED);
            device_manager_update_wifi(false, 0);
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
    ESP_LOGI(TAG, "📥 RX: size=%d, ptt=%d, call=%d", opus_size, remote_ptt_active, remote_call_active);
    
    // Update device manager with packet receipt (for sleep timeout)
    device_manager_packet_received();
    ESP_LOGD(TAG, "  ✓ Device manager updated");
    
#if DEVICE_TYPE_PACK
    // Update power manager (keep awake)
    power_manager_activity();
#endif
    
    // Update call state based on remote signal
    call_module_remote_signal(remote_call_active);
    ESP_LOGD(TAG, "  ✓ Call state updated");
    
#if DEVICE_TYPE_BASE
    // Mirror pack's PTT state on base LED
    gpio_control_set_led(LED_PTT_MIRROR, remote_ptt_active ? LED_ON : LED_OFF);
    ESP_LOGD(TAG, "  ✓ PTT mirror LED updated");
#endif
    
    // Decode and play audio
    ESP_LOGD(TAG, "  ⏳ Decoding Opus...");
    int16_t pcm_output[SAMPLES_PER_FRAME];
    int decoded = audio_opus_decode(opus_data, opus_size, pcm_output, SAMPLES_PER_FRAME, 0);
    ESP_LOGD(TAG, "  ✓ Decoded %d samples", decoded);
    
    if (decoded > 0) {
        // Apply limiting
        audio_processor_limit(pcm_output, decoded, LIMITER_THRESHOLD);
        ESP_LOGD(TAG, "  ✓ Limited");
        
        // Write to speaker/headset
        ESP_LOGD(TAG, "  ⏳ Writing to codec...");
        audio_codec_write(pcm_output, decoded);
        ESP_LOGD(TAG, "  ✓ Written to codec");
    }
    
    ESP_LOGD(TAG, "📥 RX complete");
}

static void ptt_state_handler(ptt_state_t state, bool transmitting)
{
    ESP_LOGI(TAG, "PTT: %s", transmitting ? "ON" : "OFF");
    
    // Update device manager with PTT state
    device_manager_set_ptt_state(state);
    
    // Update LED
#if DEVICE_TYPE_PACK
    gpio_control_set_led(LED_PTT, transmitting ? LED_ON : LED_OFF);
    
    // CRITICAL: Send immediate status packet when PTT turns OFF
    // Otherwise base LED stays on until next packet (which never comes!)
    if (!transmitting) {
        // Send a silent packet with PTT=false flag
        uint8_t silent_packet[60] = {0};  // Empty Opus packet (silence)
        bool call_active = call_module_is_calling();
        udp_transport_send(silent_packet, 60, false, call_active);
        ESP_LOGI(TAG, "Sent PTT-OFF notification packet");
    }
#else
    // Base mirrors pack's PTT state on PTT_MIRROR LED
    gpio_control_set_led(LED_PTT_MIRROR, transmitting ? LED_ON : LED_OFF);
#endif
    
#if DEVICE_TYPE_PACK
    // Keep device awake during PTT
    if (transmitting) {
        power_manager_activity();
    }
#endif
}

static void call_state_handler(call_state_t state, bool is_calling)
{
    const char *state_str = 
        state == CALL_IDLE ? "IDLE" :
        state == CALL_OUTGOING ? "CALLING" :
        state == CALL_INCOMING ? "INCOMING" : "ACKNOWLEDGED";
    
    ESP_LOGI(TAG, "Call: %s", state_str);
    
    // Update call LED
    if (state == CALL_OUTGOING) {
        gpio_control_set_led(LED_CALL, LED_BLINK_SLOW);  // Blink while calling out
    } else if (state == CALL_INCOMING) {
        gpio_control_set_led(LED_CALL, LED_ON);  // Solid ON for incoming call
    } else if (state == CALL_ACKNOWLEDGED) {
        gpio_control_set_led(LED_CALL, LED_ON);  // Solid ON when acknowledged
    } else {
        gpio_control_set_led(LED_CALL, LED_OFF);
    }
}

#if DEVICE_TYPE_PACK
static void ptt_button_handler(bool pressed, uint32_t hold_time_ms)
{
    ptt_control_button_event(pressed, hold_time_ms);
    
    // Keep device awake
    power_manager_activity();
}

static void call_button_handler(bool pressed)
{
    call_module_button_event(pressed);
    
    // Keep device awake
    power_manager_activity();
}

static void battery_status_handler(float voltage, uint8_t percent,
                                   bool is_low, bool is_critical)
{
    device_manager_update_battery(voltage);
    
    // Play warning tones
    if (is_critical && TONE_BATTERY_CRITICAL_ENABLE) {
        // Play critical battery tone
        audio_tones_play(TONE_BATTERY_CRITICAL.frequency_hz, 
                        TONE_BATTERY_CRITICAL.duration_ms, 0.3f);
    } else if (is_low && TONE_BATTERY_LOW_ENABLE) {
        // Play low battery tone
        audio_tones_play(TONE_BATTERY_LOW.frequency_hz,
                        TONE_BATTERY_LOW.duration_ms, 0.3f);
    }
}

static void power_state_handler(power_state_t state)
{
    ESP_LOGI(TAG, "Power state: %s",
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
        // Read audio from mic or party line
        esp_err_t ret = audio_codec_read(pcm_input, SAMPLES_PER_FRAME, NULL);
        
        if (ret == ESP_OK) {
            // Check if we should transmit
            bool should_transmit = ptt_control_is_transmitting();
            
            if (should_transmit) {
                // Encode to Opus
                int encoded_bytes = audio_opus_encode(pcm_input, SAMPLES_PER_FRAME,
                                                     opus_data, OPUS_MAX_PACKET_SIZE);
                
                if (encoded_bytes > 0) {
                    // Get call state
                    bool call_active = call_module_is_calling();
                    
                    // Send over network
                    udp_transport_send(opus_data, encoded_bytes, true, call_active);
                }
            }
        }
        
        // Run at frame rate (20ms)
        vTaskDelay(pdMS_TO_TICKS(FRAME_SIZE_MS));
    }
}

//=============================================================================
// MONITORING TASK
//=============================================================================

static void monitor_task(void *arg)
{
    ESP_LOGI(TAG, "Monitor task started");
    
    TickType_t last_wake = xTaskGetTickCount();
    uint32_t stats_counter = 0;
    
#if DEVICE_TYPE_PACK
    // PTT stuck detection
    static uint32_t ptt_transmit_time = 0;
    const uint32_t PTT_TIMEOUT_SECONDS = 300;  // 5 minutes = likely stuck
#endif
    
    while (1) {
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(1000));  // 1Hz
        
        stats_counter++;
        
#if DEVICE_TYPE_PACK
        // Monitor PTT for stuck state
        if (ptt_control_is_transmitting()) {
            ptt_transmit_time++;
            
            // Warn if transmitting for a very long time
            if (ptt_transmit_time == 60) {  // 1 minute
                ESP_LOGW(TAG, "⚠️  PTT has been active for 1 minute");
            } else if (ptt_transmit_time == 180) {  // 3 minutes
                ESP_LOGW(TAG, "⚠️  PTT has been active for 3 minutes - possible stuck state");
            } else if (ptt_transmit_time >= PTT_TIMEOUT_SECONDS) {
                ESP_LOGE(TAG, "🔴 PTT STUCK! Forcing reset to IDLE");
                ptt_control_force_idle();
                ptt_transmit_time = 0;
            }
        } else {
            ptt_transmit_time = 0;  // Reset counter when not transmitting
        }
#endif
        
        // Print status every 5 seconds
        if (stats_counter % 5 == 0) {
            // Update WiFi status with current RSSI
            if (wifi_manager_is_connected()) {
                int8_t rssi = wifi_manager_get_rssi();
                device_manager_update_wifi(true, rssi);
            }
            
            device_manager_print_status();
            
            // Print network stats
            udp_stats_t stats;
            udp_transport_get_stats(&stats);
            
            ESP_LOGI(TAG, "Network: TX=%lu, RX=%lu, Loss=%.2f%%",
                     (unsigned long)stats.packets_sent,
                     (unsigned long)stats.packets_received,
                     stats.packet_loss_percent);
        }
        
#if DEVICE_TYPE_PACK
        // Check for sleep timeout
        bool light_sleep, deep_sleep;
        power_manager_check_timeout(&light_sleep, &deep_sleep);
        
        if (deep_sleep) {
            ESP_LOGW(TAG, "Deep sleep timeout - shutting down");
            power_manager_enter_deep_sleep();
            // Never returns
        } else if (light_sleep) {
            ESP_LOGI(TAG, "Light sleep timeout - entering sleep");
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
    
    // Device manager
    ESP_LOGI(TAG, "Initializing device manager...");
    ret = device_manager_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Device manager init failed");
        return ret;
    }
    
    // Audio subsystem
    ESP_LOGI(TAG, "Initializing audio...");
    ret = audio_codec_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Audio codec init failed");
        return ret;
    }
    
    ret = audio_opus_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Opus init failed");
        return ret;
    }
    
    ret = audio_processor_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Audio processor init failed");
        return ret;
    }
    
    ret = audio_tones_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Tone generator init failed");
        return ret;
    }
    
    // Network subsystem
    ESP_LOGI(TAG, "Initializing network...");
    ret = wifi_manager_init(wifi_event_handler);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi manager init failed");
        return ret;
    }
    
    ret = wifi_manager_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi start failed");
        return ret;
    }
    
    ret = udp_transport_init(udp_rx_handler);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "UDP transport init failed");
        return ret;
    }
    
    ret = udp_transport_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "UDP transport start failed");
        return ret;
    }
    
    // Hardware subsystem
    ESP_LOGI(TAG, "Initializing hardware...");
    
#if DEVICE_TYPE_PACK
    ret = gpio_control_init(ptt_button_handler, call_button_handler);
#else
    ret = gpio_control_init(NULL, NULL);  // Base has no buttons
#endif
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "GPIO control init failed");
        return ret;
    }
    
    ret = ptt_control_init(ptt_state_handler);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "PTT control init failed");
        return ret;
    }
    
#if DEVICE_TYPE_PACK
    ret = battery_init(battery_status_handler);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Battery init failed");
        return ret;
    }
    
    ret = battery_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Battery start failed");
        return ret;
    }
#endif
    
#if DEVICE_TYPE_BASE
    ret = clearcom_line_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ClearCom line init failed");
        return ret;
    }
    
    ret = clearcom_line_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ClearCom line start failed");
        return ret;
    }
#endif
    
    // System services
    ESP_LOGI(TAG, "Initializing system services...");
    
    ret = call_module_init(call_state_handler);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Call module init failed");
        return ret;
    }
    
#if DEVICE_TYPE_PACK
    ret = power_manager_init(power_state_handler);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Power manager init failed");
        return ret;
    }
#endif
    
    return ESP_OK;
}

//=============================================================================
// MAIN ENTRY POINT
//=============================================================================

void app_main(void)
{
    esp_err_t ret;
    
    // Print banner
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  ClearCom Wireless System");
    ESP_LOGI(TAG, "  %s", DEVICE_TYPE_STRING);
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "Firmware: %s", FIRMWARE_VERSION);
    ESP_LOGI(TAG, "Build: %s %s", BUILD_DATE, BUILD_TIME);
    ESP_LOGI(TAG, "Device ID: 0x%02X", DEVICE_ID);
#if DEVICE_TYPE_BASE
    ESP_LOGI(TAG, "Paired Pack: 0x%02X", PAIRED_PACK_ID);
#else
    ESP_LOGI(TAG, "Paired Base: 0x%02X", PAIRED_BASE_ID);
#endif
    ESP_LOGI(TAG, "========================================");
    
    // Print system info
    diagnostics_print_system_info();
    
    // Initialize NVS
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "Erasing NVS flash...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    // Set log level
    esp_log_level_set("*", LOG_LEVEL);
    
    // Initialize all subsystems FIRST
    ESP_LOGI(TAG, "Initializing subsystems...");
    ret = init_subsystems();
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Subsystem initialization failed!");
        while(1) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
    
    // NOW run comprehensive self-test (all subsystems are initialized)
    ESP_LOGI(TAG, "Running system self-test...");
    diagnostics_result_t diag_results;
    ret = diagnostics_run_self_test(&diag_results);
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Self-test failed!");
        // System will halt in diagnostics if critical failure
    }
    
    // Wait for WiFi connection
    ESP_LOGI(TAG, "Waiting for WiFi connection...");
    vTaskDelay(pdMS_TO_TICKS(3000));
    
    // Set audio configuration
#if DEVICE_TYPE_PACK
    audio_codec_set_input(CODEC_INPUT_MIC);
    audio_codec_set_output(CODEC_OUTPUT_SPEAKER);
    audio_codec_set_input_gain(MIC_GAIN_LEVEL);
#else
    audio_codec_set_input(CODEC_INPUT_LINE);
    audio_codec_set_output(CODEC_OUTPUT_LINE);
    audio_codec_set_input_gain(PARTYLINE_INPUT_GAIN);
#endif
    
    // Set brightness
    gpio_control_set_brightness(LED_BRIGHTNESS_PCT);
    
    // Turn on status LED
    gpio_control_set_led(LED_STATUS, LED_ON);
    
    // Start tasks
    ESP_LOGI(TAG, "Starting tasks...");
    xTaskCreate(audio_task, "audio", 32768, NULL, 6, NULL);
    xTaskCreate(monitor_task, "monitor", 4096, NULL, 3, NULL);
    
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  System Ready");
    ESP_LOGI(TAG, "========================================");
    
#if DEVICE_TYPE_PACK
    ESP_LOGI(TAG, "DEBUG: DEVICE_TYPE_PACK = %d", DEVICE_TYPE_PACK);
    #ifdef TEST_MODE_ENABLE
        ESP_LOGI(TAG, "DEBUG: TEST_MODE_ENABLE = %d", TEST_MODE_ENABLE);
    #else
        ESP_LOGI(TAG, "DEBUG: TEST_MODE_ENABLE is NOT DEFINED");
    #endif
#endif
    
#if DEVICE_TYPE_BASE && TEST_MODE_ENABLE
    // Start test mode for party line testing
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "⚠️  TEST MODE ENABLED - Starting in 2 seconds...");
    vTaskDelay(pdMS_TO_TICKS(2000));
    test_mode_start();
#elif DEVICE_TYPE_PACK && TEST_MODE_ENABLE
    // Start test mode for headphone testing
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "⚠️  TEST MODE ENABLED - Starting in 2 seconds...");
    ESP_LOGI(TAG, "🎧 Connect headphones to pack");
    vTaskDelay(pdMS_TO_TICKS(2000));
    ESP_LOGI(TAG, "DEBUG: About to call test_mode_start()...");
    esp_err_t test_ret = test_mode_start();
    ESP_LOGI(TAG, "DEBUG: test_mode_start() returned: %d", ret);
#endif
    
    device_manager_set_state(DEVICE_STATE_CONNECTED);
}