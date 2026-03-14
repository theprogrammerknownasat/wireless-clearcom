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
static adc_oneshot_unit_handle_t adc_handle = NULL;

#if (BATTERY_MODE == BATTERY_INTERNAL)
static bool running = false;
static battery_callback_t user_callback = NULL;
static TaskHandle_t battery_task_handle = NULL;

static float current_voltage = BATTERY_FULL_VOLTAGE;
static uint8_t current_percent = 100;
static bool is_low = false;
static bool is_critical = false;

//=============================================================================
// PRIVATE FUNCTIONS (BATTERY_INTERNAL only)
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
    // ESP32-S3 ADC with ADC_ATTEN_DB_12: input range ~0-3.3V
    // 12-bit resolution (0-4095)
    // With voltage divider: Vbat = (ADC_reading / 4095) * 3.3V * divider_ratio
    // Assuming 2:1 voltage divider (adjust for your hardware)
    const float divider_ratio = 2.0f;

    float voltage = (adc_reading / 4095.0f) * 3.3f * divider_ratio;

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
#endif // BATTERY_MODE == BATTERY_INTERNAL

//=============================================================================
// PUBLIC FUNCTIONS
//=============================================================================

esp_err_t battery_init(battery_callback_t callback)
{
    if (initialized) {
        ESP_LOGW(TAG, "Battery already initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing battery (mode=%d)...", BATTERY_MODE);

    // Always create the ADC unit handle -- volume control shares it
    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = ADC_UNIT_1,
    };

    esp_err_t ret = adc_oneshot_new_unit(&init_config, &adc_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ADC init failed: %d", ret);
        return ret;
    }

#if (BATTERY_MODE == BATTERY_INTERNAL)
    user_callback = callback;

    // Configure battery ADC channel (only needed for internal battery monitoring)
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

    ESP_LOGI(TAG, "Battery monitoring initialized (ADC channel %d)", BATTERY_ADC_CHANNEL);
#else
    (void)callback;  // Unused when not BATTERY_INTERNAL
    ESP_LOGI(TAG, "ADC unit initialized (battery monitoring disabled, mode=%d)", BATTERY_MODE);
#endif

    initialized = true;
    return ESP_OK;
}

esp_err_t battery_start(void)
{
#if (BATTERY_MODE == BATTERY_INTERNAL)
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
#endif

    return ESP_OK;
}

esp_err_t battery_stop(void)
{
#if (BATTERY_MODE == BATTERY_INTERNAL)
    if (!running) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Stopping battery monitoring...");

    running = false;

    if (battery_task_handle) {
        vTaskDelay(pdMS_TO_TICKS(100));
        battery_task_handle = NULL;
    }
#endif

    return ESP_OK;
}

float battery_get_voltage(void)
{
#if (BATTERY_MODE == BATTERY_INTERNAL)
    return current_voltage;
#else
    return 0.0f;
#endif
}

uint8_t battery_get_percent(void)
{
#if (BATTERY_MODE == BATTERY_INTERNAL)
    return current_percent;
#else
    return 0;
#endif
}

bool battery_is_low(void)
{
#if (BATTERY_MODE == BATTERY_INTERNAL)
    return is_low;
#else
    return false;
#endif
}

bool battery_is_critical(void)
{
#if (BATTERY_MODE == BATTERY_INTERNAL)
    return is_critical;
#else
    return false;
#endif
}

float battery_read_voltage_once(void)
{
#if (BATTERY_MODE == BATTERY_INTERNAL)
    if (!initialized || !adc_handle) {
        return -1.0f;
    }
    return read_battery_voltage();
#else
    return -1.0f;
#endif
}

void battery_deinit(void)
{
    if (!initialized) {
        return;
    }

    ESP_LOGI(TAG, "Deinitializing battery...");

    battery_stop();

    if (adc_handle) {
        adc_oneshot_del_unit(adc_handle);
        adc_handle = NULL;
    }

    initialized = false;
    ESP_LOGI(TAG, "Battery deinitialized");
}

void *battery_get_adc_handle(void)
{
    return (void *)adc_handle;
}

#endif // DEVICE_TYPE_PACK
