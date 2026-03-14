/**
 * @file volume_control.c
 * @brief Volume Potentiometer Control Implementation
 *
 * Reads a 10k potentiometer via ADC1_CH1 (GPIO2) and maps the
 * reading to a 0-127 output volume level for the WM8960 codec.
 * 128 steps (~0.6dB each) for smooth, analog-feel control.
 *
 * Shares the ADC1 unit handle with battery.c to avoid conflicts.
 */

#include "volume_control.h"
#include "../config.h"

#if DEVICE_TYPE_PACK

#include "battery.h"
#include "../audio/audio_codec.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "VOLUME";

//=============================================================================
// CONFIGURATION
//=============================================================================

#define VOLUME_POLL_MS          50      // Read pot every 50ms for smoother response
#define VOLUME_MAX              127     // WM8960 register range (0x00-0x7F)
#define ADC_MAX                 4095
#define ADC_DEADBAND            16      // ~0.5 volume steps worth of hysteresis
#define EMA_ALPHA_SHIFT         2       // EMA weight = 0.25

//=============================================================================
// PRIVATE VARIABLES
//=============================================================================

static bool initialized = false;
static bool running = false;
static TaskHandle_t volume_task_handle = NULL;
static adc_oneshot_unit_handle_t adc_handle = NULL;

static uint8_t current_volume = 0;
static int smoothed_adc = -1;

//=============================================================================
// PRIVATE FUNCTIONS
//=============================================================================

static uint8_t adc_to_volume(int adc_val)
{
    if (adc_val <= 0) return 0;
    if (adc_val >= ADC_MAX) return VOLUME_MAX;

    // Linear mapping: 0-4095 -> 0-127
    return (uint8_t)((uint32_t)adc_val * VOLUME_MAX / ADC_MAX);
}

static void volume_task(void *arg)
{
    ESP_LOGI(TAG, "Volume control task started");

    while (running) {
        int raw = 0;
        esp_err_t ret = adc_oneshot_read(adc_handle, VOLUME_ADC_CHANNEL, &raw);
        if (ret != ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(VOLUME_POLL_MS));
            continue;
        }

        // Seed EMA on first reading
        if (smoothed_adc < 0) {
            smoothed_adc = raw;
        }

        // Exponential moving average
        smoothed_adc += (raw - smoothed_adc) >> EMA_ALPHA_SHIFT;

        uint8_t new_vol = adc_to_volume(smoothed_adc);
        if (new_vol != current_volume) {
            // Hysteresis: require ADC to move past deadband before changing
            int current_centre = (int)current_volume * ADC_MAX / VOLUME_MAX;
            int distance = smoothed_adc - current_centre;
            if (distance < 0) distance = -distance;

            if (distance >= ADC_DEADBAND || new_vol == 0 || new_vol == VOLUME_MAX) {
                current_volume = new_vol;
                audio_codec_set_output_volume(current_volume);
                // Only log on significant changes (every ~4 steps) to reduce UART noise
                if (current_volume % 4 == 0 || current_volume == VOLUME_MAX) {
                    ESP_LOGD(TAG, "Vol: %d/127 (ADC: %d)", current_volume, smoothed_adc);
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(VOLUME_POLL_MS));
    }

    ESP_LOGI(TAG, "Volume control task stopped");
    vTaskDelete(NULL);
}

//=============================================================================
// PUBLIC FUNCTIONS
//=============================================================================

esp_err_t volume_control_init(void)
{
    if (initialized) {
        ESP_LOGW(TAG, "Volume control already initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing volume control...");

    adc_handle = (adc_oneshot_unit_handle_t)battery_get_adc_handle();
    if (adc_handle == NULL) {
        ESP_LOGE(TAG, "ADC handle not available -- is battery_init() called first?");
        return ESP_FAIL;
    }

    adc_oneshot_chan_cfg_t chan_config = {
        .bitwidth = ADC_BITWIDTH_12,
        .atten = ADC_ATTEN_DB_12,
    };

    esp_err_t ret = adc_oneshot_config_channel(adc_handle, VOLUME_ADC_CHANNEL, &chan_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ADC channel config failed: %d", ret);
        adc_handle = NULL;
        return ret;
    }

    initialized = true;
    ESP_LOGI(TAG, "Volume control initialized (ADC channel %d)", VOLUME_ADC_CHANNEL);

    return ESP_OK;
}

esp_err_t volume_control_start(void)
{
    if (!initialized) {
        ESP_LOGE(TAG, "Volume control not initialized");
        return ESP_FAIL;
    }

    if (running) {
        return ESP_OK;
    }

    running = true;
    xTaskCreate(volume_task, "vol_ctrl", 4096, NULL, 3, &volume_task_handle);

    return ESP_OK;
}

esp_err_t volume_control_stop(void)
{
    if (!running) return ESP_OK;

    running = false;
    if (volume_task_handle) {
        vTaskDelay(pdMS_TO_TICKS(200));
        volume_task_handle = NULL;
    }
    return ESP_OK;
}

uint8_t volume_control_get_level(void)
{
    return current_volume;
}

void volume_control_deinit(void)
{
    if (!initialized) return;
    volume_control_stop();
    adc_handle = NULL;
    initialized = false;
}

#endif // DEVICE_TYPE_PACK
