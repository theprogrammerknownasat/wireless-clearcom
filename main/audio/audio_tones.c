/**
 * @file audio_tones.c
 * @brief Audio Tone Generation Implementation
 */

#include "audio_tones.h"

#include <esp_timer.h>

#include "../config.h"
#include "esp_log.h"
#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static const char *TAG = "TONES";

//=============================================================================
// PRIVATE VARIABLES
//=============================================================================

static bool tone_playing = false;
static uint16_t current_frequency = 0;
static uint16_t current_duration = 0;
static float current_amplitude = 0.0f;
static int64_t tone_start_time = 0;

//=============================================================================
// PUBLIC FUNCTIONS
//=============================================================================

void audio_tones_generate_sine(int16_t *buffer, size_t sample_count,
                               float frequency_hz, float amplitude,
                               float *phase)
{
    if (!buffer || !phase) {
        return;
    }

    // Clamp amplitude
    if (amplitude > 1.0f) amplitude = 1.0f;
    if (amplitude < 0.0f) amplitude = 0.0f;

    const float phase_increment = 2.0f * M_PI * frequency_hz / SAMPLE_RATE_HZ;
    const float peak_value = 32767.0f * amplitude;

    for (size_t i = 0; i < sample_count; i++) {
        buffer[i] = (int16_t)(peak_value * sinf(*phase));
        *phase += phase_increment;

        // Wrap phase to prevent overflow
        if (*phase >= 2.0f * M_PI) {
            *phase -= 2.0f * M_PI;
        }
    }
}

esp_err_t audio_tones_play(uint16_t frequency_hz, uint16_t duration_ms, float amplitude)
{
    ESP_LOGD(TAG, "Playing tone: %d Hz, %d ms, %.2f amplitude",
             frequency_hz, duration_ms, amplitude);

    current_frequency = frequency_hz;
    current_duration = duration_ms;
    current_amplitude = amplitude;
    tone_start_time = esp_timer_get_time() / 1000; // Convert to ms
    tone_playing = true;

    return ESP_OK;
}

void audio_tones_stop(void)
{
    if (tone_playing) {
        ESP_LOGD(TAG, "Stopping tone");
        tone_playing = false;
    }
}

bool audio_tones_is_playing(void)
{
    if (!tone_playing) {
        return false;
    }

    // Check if duration expired
    int64_t now = esp_timer_get_time() / 1000;
    int64_t elapsed = now - tone_start_time;

    if (elapsed >= current_duration) {
        tone_playing = false;
        return false;
    }

    return true;
}

esp_err_t audio_tones_init(void)
{
    ESP_LOGI(TAG, "Tone generator initialized");
    return ESP_OK;
}