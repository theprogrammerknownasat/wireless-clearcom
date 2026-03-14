/**
 * @file volume_control.c
 * @brief Volume Potentiometer Control Implementation
 *
 * Reads a 10k potentiometer via ADC1_CH1 (GPIO2) and maps the
 * reading to a 0-31 output volume level for the WM8960 codec.
 *
 * Shares the ADC1 unit handle with battery.c to avoid conflicts
 * (ESP-IDF only allows one handle per ADC unit).
 *
 * Smoothing: uses an exponential moving average plus a deadband
 * (hysteresis) to prevent jittery volume jumps -- similar feel
 * to a real RS-701 belt pack volume knob.
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

#define VOLUME_POLL_MS          100     // Read pot every 100ms
#define VOLUME_LEVELS           32      // 0-31
#define ADC_MAX                 4095
#define ADC_DEADBAND            50      // Ignore changes smaller than this
#define EMA_ALPHA_SHIFT         2       // EMA weight = 1/(1<<shift) = 0.25

//=============================================================================
// PRIVATE VARIABLES
//=============================================================================

static bool initialized = false;
static bool running = false;
static TaskHandle_t volume_task_handle = NULL;
static adc_oneshot_unit_handle_t adc_handle = NULL;

static uint8_t current_volume = 0;
static int smoothed_adc = -1;          // -1 = not yet initialized

//=============================================================================
// PRIVATE FUNCTIONS
//=============================================================================

/**
 * @brief Map a smoothed ADC reading (0-4095) to volume (0-31)
 *
 * At minimum position (ADC ~0) volume is 0 (silent).
 * At maximum position (ADC ~4095) volume is 31 (full).
 */
static uint8_t adc_to_volume(int adc_val)
{
    if (adc_val <= 0) return 0;
    if (adc_val >= ADC_MAX) return VOLUME_LEVELS - 1;

    // Linear mapping: volume = adc * 31 / 4095
    uint8_t vol = (uint8_t)((uint32_t)adc_val * (VOLUME_LEVELS - 1) / ADC_MAX);
    return vol;
}

static void volume_task(void *arg)
{
    ESP_LOGI(TAG, "Volume control task started");

    while (running) {
        int raw = 0;
        esp_err_t ret = adc_oneshot_read(adc_handle, VOLUME_ADC_CHANNEL, &raw);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "ADC read failed: %d", ret);
            vTaskDelay(pdMS_TO_TICKS(VOLUME_POLL_MS));
            continue;
        }

        // First reading -- seed the EMA
        if (smoothed_adc < 0) {
            smoothed_adc = raw;
        }

        // Exponential moving average:
        //   smoothed = smoothed + (raw - smoothed) / 4
        smoothed_adc += (raw - smoothed_adc) >> EMA_ALPHA_SHIFT;

        // Deadband: only act if the smoothed value changed enough
        uint8_t new_vol = adc_to_volume(smoothed_adc);
        if (new_vol != current_volume) {
            // Extra hysteresis: require the raw ADC to have moved
            // at least ADC_DEADBAND from the centre of the current
            // volume step before accepting a change.
            int current_centre = (int)current_volume * ADC_MAX / (VOLUME_LEVELS - 1);
            int distance = smoothed_adc - current_centre;
            if (distance < 0) distance = -distance;

            if (distance >= ADC_DEADBAND || new_vol == 0 || new_vol == (VOLUME_LEVELS - 1)) {
                current_volume = new_vol;
                audio_codec_set_output_volume(current_volume);
                ESP_LOGI(TAG, "Volume: %d/31 (ADC: %d)", current_volume, smoothed_adc);
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

    // Get the shared ADC1 handle from battery module
    adc_handle = (adc_oneshot_unit_handle_t)battery_get_adc_handle();
    if (adc_handle == NULL) {
        ESP_LOGE(TAG, "ADC handle not available -- is battery_init() called first?");
        return ESP_FAIL;
    }

    // Configure ADC channel for the volume potentiometer
    adc_oneshot_chan_cfg_t chan_config = {
        .bitwidth = ADC_BITWIDTH_12,
        .atten = ADC_ATTEN_DB_12,  // 0-3.3V range (pot wiper spans 0-3.3V)
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
        ESP_LOGW(TAG, "Volume control already running");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Starting volume control...");

    running = true;
    xTaskCreate(volume_task, "vol_ctrl", 2048, NULL, 3, &volume_task_handle);

    return ESP_OK;
}

esp_err_t volume_control_stop(void)
{
    if (!running) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Stopping volume control...");

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
    if (!initialized) {
        return;
    }

    ESP_LOGI(TAG, "Deinitializing volume control...");

    volume_control_stop();

    // Don't delete the ADC unit -- battery owns it
    adc_handle = NULL;
    initialized = false;

    ESP_LOGI(TAG, "Volume control deinitialized");
}

#endif // DEVICE_TYPE_PACK
