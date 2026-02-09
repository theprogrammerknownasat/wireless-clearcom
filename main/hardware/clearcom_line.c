/**
 * @file clearcom_line.c
 * @brief ClearCom Party Line Interface Implementation
 */

#include "clearcom_line.h"
#include "../config.h"

#if DEVICE_TYPE_BASE

#include "../audio/audio_codec.h"
#include "../audio/audio_processor.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "CC_LINE";

//=============================================================================
// PRIVATE VARIABLES
//=============================================================================

static bool initialized = false;
static bool running = false;
static clearcom_line_status_t status = {0};

//=============================================================================
// PRIVATE FUNCTIONS
//=============================================================================

static bool detect_dc_offset(const int16_t *buffer, size_t sample_count)
{
    // Calculate DC offset (average of signal)
    int32_t sum = 0;
    for (size_t i = 0; i < sample_count; i++) {
        sum += buffer[i];
    }

    int16_t dc_offset = sum / sample_count;

    // If DC offset is significant (>10% of max), flag as fault
    const int16_t dc_threshold = 3276;  // 10% of 32767

    return (abs(dc_offset) > dc_threshold);
}

//=============================================================================
// PUBLIC FUNCTIONS
//=============================================================================

esp_err_t clearcom_line_init(void)
{
    if (initialized) {
        ESP_LOGW(TAG, "ClearCom line already initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing ClearCom party line interface...");

    // Line interface uses WM8960 line input/output
    // Already initialized by audio_codec_init()

    // Set initial gains
    clearcom_line_set_input_gain(PARTYLINE_INPUT_GAIN);
    clearcom_line_set_output_gain(PARTYLINE_OUTPUT_GAIN);

    // Reset status
    memset(&status, 0, sizeof(status));
    status.line_connected = true;  // Assume connected (no detection circuit)

    initialized = true;
    ESP_LOGI(TAG, "ClearCom line interface initialized");

    return ESP_OK;
}

esp_err_t clearcom_line_start(void)
{
    if (!initialized) {
        ESP_LOGE(TAG, "ClearCom line not initialized");
        return ESP_FAIL;
    }

    if (running) {
        ESP_LOGW(TAG, "ClearCom line already running");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Starting ClearCom line interface...");

    // Switch codec to line input/output mode
    audio_codec_set_input(CODEC_INPUT_LINE);
    audio_codec_set_output(CODEC_OUTPUT_LINE);

    running = true;
    ESP_LOGI(TAG, "ClearCom line interface started");

    return ESP_OK;
}

esp_err_t clearcom_line_stop(void)
{
    if (!running) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Stopping ClearCom line interface...");

    running = false;

    return ESP_OK;
}

esp_err_t clearcom_line_read(int16_t *buffer, size_t sample_count)
{
    if (!initialized || !running || !buffer) {
        return ESP_FAIL;
    }

    // Read from codec line input
    esp_err_t ret = audio_codec_read(buffer, sample_count);

    if (ret == ESP_OK) {
        // Update status
        status.input_level = audio_processor_get_rms(buffer, sample_count);

        // Check for DC offset (fault condition)
        if (PARTYLINE_DC_BLOCKING) {
            status.dc_offset_detected = detect_dc_offset(buffer, sample_count);
            if (status.dc_offset_detected) {
                ESP_LOGW(TAG, "DC offset detected on party line input");
            }
        }
    }

    return ret;
}

esp_err_t clearcom_line_write(const int16_t *buffer, size_t sample_count)
{
    if (!initialized || !running || !buffer) {
        return ESP_FAIL;
    }

    // Update output level
    status.output_level = audio_processor_get_rms(buffer, sample_count);

    // Write to codec line output
    return audio_codec_write(buffer, sample_count);
}

void clearcom_line_get_status(clearcom_line_status_t *status_out)
{
    if (status_out) {
        memcpy(status_out, &status, sizeof(clearcom_line_status_t));
    }
}

void clearcom_line_set_output_gain(uint8_t gain)
{
    if (gain > 31) gain = 31;
    audio_codec_set_output_volume(gain);
    ESP_LOGI(TAG, "Party line output gain: %d", gain);
}

void clearcom_line_set_input_gain(uint8_t gain)
{
    if (gain > 31) gain = 31;
    audio_codec_set_input_gain(gain);
    ESP_LOGI(TAG, "Party line input gain: %d", gain);
}

void clearcom_line_deinit(void)
{
    if (!initialized) {
        return;
    }

    ESP_LOGI(TAG, "Deinitializing ClearCom line interface...");

    clearcom_line_stop();

    initialized = false;
    ESP_LOGI(TAG, "ClearCom line interface deinitialized");
}

#endif // DEVICE_TYPE_BASE