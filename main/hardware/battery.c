/**
 * @file battery.c
 * @brief Battery Monitoring Implementation
 */

#include "battery.h"
#include "../config.h"

#if DEVICE_TYPE_PACK

#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "BATTERY";

//=============================================================================
// PRIVATE VARIABLES
//=============================================================================

static bool initialized = false;
static bool running = false;
static adc_oneshot_unit_handle_t adc_handle = NULL;
static battery_callback_t user_callback = NULL;
static TaskHandle_t battery_task_handle = NULL;

static float current_voltage = BATTERY_FULL_VOLTAGE;
static uint8_t current_percent = 100;
static bool is_low = false;
static bool is_critical = false;

//=============================================================================
// PRIVATE FUNCTIONS
//=============================================================================

static uint8_t voltage_to_percent(float voltage)
{
    if (voltage >= BATTERY_FULL_VOLTAGE) {
        return 100;
    } else if (voltage <= BATTERY_EMPTY_VOLTAGE) {
        return 0;
    }

    // Linear approximation (for production, use lookup table)
    float range = BATTERY_FULL_VOLTAGE - BATTERY_EMPTY_VOLTAGE;
    float position = voltage - BATTERY_EMPTY_VOLTAGE;
    uint8_t percent = (uint8_t)((position / range) * 100.0f);

    if (percent > 100) percent = 100;

    return percent;
}

static float read_battery_voltage(void)
{
    int adc_reading = 0;
    esp_err_t ret = adc_oneshot_read(adc_handle, BATTERY_ADC_CHANNEL, &adc_reading);

    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "ADC read failed: %d", ret);
        return current_voltage;  // Return last known value
    }

    // Convert ADC reading to voltage
    // ESP32-S3 ADC: 12-bit (0-4095), reference voltage ~1.1V
    // With voltage divider: Vbat = ADC_reading * (1.1V / 4095) * divider_ratio
    // Assuming 2:1 voltage divider (adjust for your hardware)
    const float adc_ref = 1.1f;
    const float divider_ratio = 2.0f;

    float voltage = (adc_reading / 4095.0f) * adc_ref * divider_ratio;

    return voltage;
}

static void battery_task(void *arg)
{
    ESP_LOGI(TAG, "Battery monitoring task started");

    while (running) {
        // Read battery voltage
        float voltage = read_battery_voltage();
        uint8_t percent = voltage_to_percent(voltage);

        // Check thresholds
        bool was_low = is_low;
        bool was_critical = is_critical;

        is_low = (voltage <= BATTERY_LOW_VOLTAGE);
        is_critical = (voltage <= BATTERY_CRITICAL_VOLTAGE);

        // Update current values
        current_voltage = voltage;
        current_percent = percent;

        // Log state changes
        if (is_low && !was_low) {
            ESP_LOGW(TAG, "Battery LOW: %.2fV (%d%%)", voltage, percent);
        }
        if (is_critical && !was_critical) {
            ESP_LOGE(TAG, "Battery CRITICAL: %.2fV (%d%%)", voltage, percent);
        }

        // Call user callback
        if (user_callback) {
            user_callback(voltage, percent, is_low, is_critical);
        }

        ESP_LOGD(TAG, "Battery: %.2fV (%d%%)", voltage, percent);

        // Wait before next reading
        vTaskDelay(pdMS_TO_TICKS(BATTERY_CHECK_INTERVAL_SEC * 1000));
    }

    ESP_LOGI(TAG, "Battery monitoring task stopped");
    vTaskDelete(NULL);
}

//=============================================================================
// PUBLIC FUNCTIONS
//=============================================================================

esp_err_t battery_init(battery_callback_t callback)
{
    if (initialized) {
        ESP_LOGW(TAG, "Battery monitoring already initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing battery monitoring...");

    user_callback = callback;

    // Configure ADC
    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = ADC_UNIT_1,
    };

    esp_err_t ret = adc_oneshot_new_unit(&init_config, &adc_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ADC init failed: %d", ret);
        return ret;
    }

    adc_oneshot_chan_cfg_t chan_config = {
        .bitwidth = ADC_BITWIDTH_12,
        .atten = ADC_ATTEN_DB_12,  // 0-3.3V range
    };

    ret = adc_oneshot_config_channel(adc_handle, BATTERY_ADC_CHANNEL, &chan_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ADC channel config failed: %d", ret);
        adc_oneshot_del_unit(adc_handle);
        adc_handle = NULL;
        return ret;
    }

    initialized = true;
    ESP_LOGI(TAG, "Battery monitoring initialized (ADC channel %d)", BATTERY_ADC_CHANNEL);

    return ESP_OK;
}

esp_err_t battery_start(void)
{
    if (!initialized) {
        ESP_LOGE(TAG, "Battery monitoring not initialized");
        return ESP_FAIL;
    }

    if (running) {
        ESP_LOGW(TAG, "Battery monitoring already running");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Starting battery monitoring...");

    running = true;
    xTaskCreate(battery_task, "battery", 4096, NULL, 2, &battery_task_handle);

    return ESP_OK;
}

esp_err_t battery_stop(void)
{
    if (!running) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Stopping battery monitoring...");

    running = false;

    if (battery_task_handle) {
        vTaskDelay(pdMS_TO_TICKS(100));
        battery_task_handle = NULL;
    }

    return ESP_OK;
}

float battery_get_voltage(void)
{
    return current_voltage;
}

uint8_t battery_get_percent(void)
{
    return current_percent;
}

bool battery_is_low(void)
{
    return is_low;
}

bool battery_is_critical(void)
{
    return is_critical;
}

void battery_deinit(void)
{
    if (!initialized) {
        return;
    }

    ESP_LOGI(TAG, "Deinitializing battery monitoring...");

    battery_stop();

    if (adc_handle) {
        adc_oneshot_del_unit(adc_handle);
        adc_handle = NULL;
    }

    initialized = false;
    ESP_LOGI(TAG, "Battery monitoring deinitialized");
}

#endif // DEVICE_TYPE_PACK