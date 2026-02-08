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

static const char *TAG = "MAIN";

//=============================================================================
// TASK DECLARATIONS
//=============================================================================

static void stats_task(void *arg);

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

    // Initialize NVS (required for Wi-Fi)
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

    ESP_LOGI(TAG, "Phase 1: Foundation complete");
    ESP_LOGI(TAG, "Waiting for Phase 2: Audio subsystem...");

    // Start stats task
    xTaskCreate(stats_task, "stats", 4096, NULL, 3, NULL);

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