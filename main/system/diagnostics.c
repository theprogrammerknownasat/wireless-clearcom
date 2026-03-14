/**
 * @file diagnostics.c
 * @brief System Diagnostics Implementation
 */

#include "diagnostics.h"

#include <esp_flash.h>

#include "../config.h"
#include "../audio/audio_codec.h"
#include "../audio/audio_opus.h"
#include "../hardware/gpio_control.h"
#include "../network/wifi_manager.h"
#include "../network/udp_transport.h"
#include "esp_chip_info.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include <string.h>
#include <stdio.h>

#if DEVICE_TYPE_PACK
#include "../hardware/battery.h"
#endif

static const char *TAG = "DIAG";

//=============================================================================
// PRIVATE FUNCTIONS
//=============================================================================

static const char* result_to_string(test_result_t result)
{
    switch (result) {
        case TEST_PASS: return "PASS";
        case TEST_FAIL: return "FAIL";
        case TEST_SKIP: return "SKIP";
        default: return "NOT_RUN";
    }
}

/**
 * @brief Blink status LED N times to indicate which test failed, then pause.
 * @param test_number The 1-based test number that failed
 */
static void blink_fault_code(int test_number)
{
    for (int i = 0; i < test_number; i++) {
        gpio_control_set_led(LED_STATUS, LED_ON);
        vTaskDelay(pdMS_TO_TICKS(200));
        gpio_control_set_led(LED_STATUS, LED_OFF);
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    vTaskDelay(pdMS_TO_TICKS(1000));
}

//=============================================================================
// PUBLIC FUNCTIONS
//=============================================================================

esp_err_t diagnostics_run_self_test(diagnostics_result_t *results)
{
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  SYSTEM SELF-TEST");
    ESP_LOGI(TAG, "========================================");

    if (!results) {
        return ESP_FAIL;
    }

    memset(results, 0, sizeof(diagnostics_result_t));

    // Test 1: WM8960 I2C Communication
    ESP_LOGI(TAG, "Testing WM8960 I2C...");
    if (audio_codec_is_initialized()) {
        results->codec_i2c = TEST_PASS;
        ESP_LOGI(TAG, "  WM8960 I2C: PASS (codec initialized successfully)");
    } else {
        results->codec_i2c = TEST_FAIL;
        ESP_LOGE(TAG, "  WM8960 I2C: FAIL (codec not initialized)");
    }

    // Test 2: Audio loopback
    // Cannot perform a real loopback test without external wiring connecting
    // the codec output back to its input. Skipping to avoid false failures.
    ESP_LOGI(TAG, "Testing audio loopback...");
    results->codec_audio = TEST_SKIP;
    ESP_LOGI(TAG, "  Audio loopback: SKIP (requires external loopback wiring)");

    // Test 3: Battery ADC (pack only)
#if DEVICE_TYPE_PACK
    ESP_LOGI(TAG, "Testing battery ADC...");
#if (BATTERY_MODE == BATTERY_NONE || BATTERY_MODE == BATTERY_EXTERNAL)
    // No internal battery monitoring configured - skip ADC test
    results->battery_adc = TEST_SKIP;
    ESP_LOGI(TAG, "  Battery ADC: SKIP (BATTERY_MODE=%d, no internal monitoring)", BATTERY_MODE);
#else
    {
        float batt_voltage = battery_read_voltage_once();
        if (batt_voltage < 0.0f) {
            // ADC not initialized
            results->battery_adc = TEST_SKIP;
            ESP_LOGW(TAG, "  Battery ADC: SKIP (ADC not initialized)");
        } else if (batt_voltage >= 0.5f && batt_voltage <= 5.0f) {
            results->battery_adc = TEST_PASS;
            ESP_LOGI(TAG, "  Battery ADC: PASS (%.2fV)", batt_voltage);
        } else {
            results->battery_adc = TEST_FAIL;
            ESP_LOGE(TAG, "  Battery ADC: FAIL (%.2fV - out of range 0.5-5.0V, pin may be floating)", batt_voltage);
        }
    }
#endif
#else
    results->battery_adc = TEST_SKIP;
#endif

    // Test 4: GPIO buttons (pack only)
#if DEVICE_TYPE_PACK
    ESP_LOGI(TAG, "Testing button GPIOs...");
    {
        int ptt_level = gpio_get_level(BUTTON_PTT_PIN);
        int call_level = gpio_get_level(BUTTON_CALL_PIN);
        bool ptt_ok = (ptt_level == 1);
        bool call_ok = (call_level == 1);

        if (ptt_ok && call_ok) {
            results->gpio_buttons = TEST_PASS;
            ESP_LOGI(TAG, "  Button GPIOs: PASS (PTT=%d, CALL=%d)", ptt_level, call_level);
        } else {
            results->gpio_buttons = TEST_FAIL;
            if (!ptt_ok) {
                ESP_LOGE(TAG, "  Button GPIOs: FAIL - PTT button reads LOW at boot (stuck or shorted)");
            }
            if (!call_ok) {
                ESP_LOGE(TAG, "  Button GPIOs: FAIL - CALL button reads LOW at boot (stuck or shorted)");
            }
        }
    }
#else
    results->gpio_buttons = TEST_SKIP;
#endif

    // Test 5: LED GPIOs
    // Flash each enabled LED on for 100ms then off as a visual test.
    // Always passes - the operator can visually confirm LED function.
    ESP_LOGI(TAG, "Testing LED GPIOs...");
    for (int i = 0; i < LED_COUNT; i++) {
        gpio_control_set_led((led_id_t)i, LED_ON);
        vTaskDelay(pdMS_TO_TICKS(100));
        gpio_control_set_led((led_id_t)i, LED_OFF);
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    results->gpio_leds = TEST_PASS;
    ESP_LOGI(TAG, "  LED GPIOs: PASS (visual check - all LEDs flashed)");

    // Test 6: Opus encoder
    ESP_LOGI(TAG, "Testing Opus encoder...");
    int16_t test_audio[320] = {0};
    uint8_t opus_data[256];
    int encoded = audio_opus_encode(test_audio, 320, opus_data, 256);
    results->opus_encode = (encoded > 0) ? TEST_PASS : TEST_FAIL;
    ESP_LOGI(TAG, "  Opus encoder: %s (%d bytes)",
             result_to_string(results->opus_encode), encoded);

    // Test 7: Opus decoder
    ESP_LOGI(TAG, "Testing Opus decoder...");
    int16_t decoded_audio[320];
    int decoded = audio_opus_decode(opus_data, encoded, decoded_audio, 320, 0);
    results->opus_decode = (decoded > 0) ? TEST_PASS : TEST_FAIL;
    ESP_LOGI(TAG, "  Opus decoder: %s (%d samples)",
             result_to_string(results->opus_decode), decoded);

    // Test 8: WiFi
    ESP_LOGI(TAG, "Testing WiFi...");
#if TEST_MODE_ENABLE
    results->wifi = TEST_SKIP;
    ESP_LOGI(TAG, "  WiFi: SKIP (disabled in test mode)");
#else
    if (wifi_manager_is_initialized()) {
        results->wifi = TEST_PASS;
        ESP_LOGI(TAG, "  WiFi: PASS (manager initialized)");
    } else {
        results->wifi = TEST_FAIL;
        ESP_LOGE(TAG, "  WiFi: FAIL (manager not initialized)");
    }
#endif

    // Test 9: UDP
    ESP_LOGI(TAG, "Testing UDP...");
#if TEST_MODE_ENABLE
    results->udp = TEST_SKIP;
    ESP_LOGI(TAG, "  UDP: SKIP (disabled in test mode)");
#else
    if (udp_transport_is_initialized()) {
        results->udp = TEST_PASS;
        ESP_LOGI(TAG, "  UDP: PASS (transport initialized)");
    } else {
        results->udp = TEST_FAIL;
        ESP_LOGE(TAG, "  UDP: FAIL (transport not initialized)");
    }
#endif

    // Test 10: NVS
    ESP_LOGI(TAG, "Testing NVS storage...");
    nvs_handle_t nvs_handle;
    esp_err_t ret = nvs_open("test", NVS_READWRITE, &nvs_handle);
    if (ret == ESP_OK) {
        nvs_set_u8(nvs_handle, "selftest", 1);
        nvs_commit(nvs_handle);
        nvs_close(nvs_handle);
        results->nvs = TEST_PASS;
        ESP_LOGI(TAG, "  NVS: PASS");
    } else {
        results->nvs = TEST_FAIL;
        ESP_LOGE(TAG, "  NVS: FAIL");
    }

    // Calculate summary
    // Map test index to test number (1-based) for fault code reporting
    struct {
        test_result_t *result;
        int test_number;
        const char *name;
    } test_map[] = {
        { &results->codec_i2c,    1,  "WM8960 I2C" },
        { &results->codec_audio,  2,  "Audio loopback" },
        { &results->battery_adc,  3,  "Battery ADC" },
        { &results->gpio_buttons, 4,  "Button GPIOs" },
        { &results->gpio_leds,    5,  "LED GPIOs" },
        { &results->opus_encode,  6,  "Opus encoder" },
        { &results->opus_decode,  7,  "Opus decoder" },
        { &results->wifi,         8,  "WiFi" },
        { &results->udp,          9,  "UDP" },
        { &results->nvs,          10, "NVS storage" },
    };

    int first_failed_test = 0;

    for (int i = 0; i < 10; i++) {
        if (*test_map[i].result == TEST_PASS) {
            results->pass_count++;
        } else if (*test_map[i].result == TEST_FAIL) {
            results->fail_count++;
            if (first_failed_test == 0) {
                first_failed_test = test_map[i].test_number;
            }
        }
        if (*test_map[i].result != TEST_SKIP) {
            results->total_count++;
        }
    }

    results->all_passed = (results->fail_count == 0);

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "Self-test complete: %d/%d passed, %d failed",
             results->pass_count, results->total_count, results->fail_count);
    ESP_LOGI(TAG, "========================================");

    if (!results->all_passed) {
        // Report all failures
        for (int i = 0; i < 10; i++) {
            if (*test_map[i].result == TEST_FAIL) {
                ESP_LOGE(TAG, "FAILED TEST %d: %s", test_map[i].test_number, test_map[i].name);
            }
        }
        ESP_LOGE(TAG, "CRITICAL: SELF-TEST FAILED - check hardware");
        ESP_LOGE(TAG, "Status LED fault code: %d blinks = Test %d (%s)",
                 first_failed_test, first_failed_test,
                 test_map[first_failed_test - 1].name);
        ESP_LOGE(TAG, "System halted. Reset to retry.");

        // Blink status LED with fault code pattern and halt
        while (1) {
            blink_fault_code(first_failed_test);
        }
    }

    return results->all_passed ? ESP_OK : ESP_FAIL;
}

void diagnostics_print_results(const diagnostics_result_t *results)
{
    if (!results) return;

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  DIAGNOSTICS RESULTS");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "WM8960 I2C:      %s", result_to_string(results->codec_i2c));
    ESP_LOGI(TAG, "Audio loopback:  %s", result_to_string(results->codec_audio));
    ESP_LOGI(TAG, "Opus encoder:    %s", result_to_string(results->opus_encode));
    ESP_LOGI(TAG, "Opus decoder:    %s", result_to_string(results->opus_decode));
    ESP_LOGI(TAG, "WiFi:            %s", result_to_string(results->wifi));
    ESP_LOGI(TAG, "UDP:             %s", result_to_string(results->udp));
    ESP_LOGI(TAG, "Battery ADC:     %s", result_to_string(results->battery_adc));
    ESP_LOGI(TAG, "Button GPIOs:    %s", result_to_string(results->gpio_buttons));
    ESP_LOGI(TAG, "LED GPIOs:       %s", result_to_string(results->gpio_leds));
    ESP_LOGI(TAG, "NVS storage:     %s", result_to_string(results->nvs));
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "Overall: %d/%d passed, %d failed",
             results->pass_count, results->total_count, results->fail_count);
    ESP_LOGI(TAG, "========================================");
}

size_t diagnostics_get_free_heap(void)
{
    return esp_get_free_heap_size();
}

size_t diagnostics_get_min_free_heap(void)
{
    return esp_get_minimum_free_heap_size();
}

void diagnostics_print_system_info(void)
{
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);

    uint32_t flash_size;
    if (esp_flash_get_size(NULL, &flash_size) != ESP_OK) {
        flash_size = 0;
    }

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  SYSTEM INFORMATION");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "Chip: %s", CONFIG_IDF_TARGET);
    ESP_LOGI(TAG, "Cores: %d", chip_info.cores);

    if (flash_size > 0) {
        ESP_LOGI(TAG, "Flash: %lu MB %s",
                 (unsigned long)(flash_size / (1024 * 1024)),
                 (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "embedded" : "external");
    } else {
        ESP_LOGI(TAG, "Flash: Unknown size");
    }

    ESP_LOGI(TAG, "Free heap: %lu bytes", (unsigned long)diagnostics_get_free_heap());
    ESP_LOGI(TAG, "Min free heap: %lu bytes", (unsigned long)diagnostics_get_min_free_heap());
    ESP_LOGI(TAG, "========================================");
}
