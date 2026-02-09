/**
 * @file audio_processor.c
 * @brief Audio Processing Pipeline Implementation
 */

#include "audio_processor.h"
#include "../config.h"
#include "esp_log.h"
#include <math.h>
#include <string.h>

static const char *TAG = "AUDIO_PROC";

//=============================================================================
// PRIVATE FUNCTIONS
//=============================================================================

static inline int16_t clamp_sample(int32_t sample)
{
    if (sample > 32767) return 32767;
    if (sample < -32768) return -32768;
    return (int16_t)sample;
}

//=============================================================================
// PUBLIC FUNCTIONS
//=============================================================================

esp_err_t audio_processor_init(void)
{
    ESP_LOGI(TAG, "Audio processor initialized");
    ESP_LOGI(TAG, "Limiter enabled: %d", ENABLE_AUDIO_LIMITER);
    ESP_LOGI(TAG, "Limiter threshold: %.2f", LIMITER_THRESHOLD);

    return ESP_OK;
}

void audio_processor_mix(const int16_t *stream1, const int16_t *stream2,
                         int16_t *output, size_t sample_count,
                         float mix1, float mix2)
{
    if (!stream1 || !stream2 || !output) {
        return;
    }

    // Clamp mix levels
    if (mix1 > 1.0f) mix1 = 1.0f;
    if (mix1 < 0.0f) mix1 = 0.0f;
    if (mix2 > 1.0f) mix2 = 1.0f;
    if (mix2 < 0.0f) mix2 = 0.0f;

    for (size_t i = 0; i < sample_count; i++) {
        int32_t mixed = (int32_t)(stream1[i] * mix1) + (int32_t)(stream2[i] * mix2);
        output[i] = clamp_sample(mixed);
    }
}

void audio_processor_limit(int16_t *buffer, size_t sample_count, float threshold)
{
    if (!buffer || !ENABLE_AUDIO_LIMITER) {
        return;
    }

    // Clamp threshold
    if (threshold > 1.0f) threshold = 1.0f;
    if (threshold < 0.0f) threshold = 0.0f;

    const int16_t threshold_value = (int16_t)(32767.0f * threshold);

    for (size_t i = 0; i < sample_count; i++) {
        if (buffer[i] > threshold_value) {
            // Soft limiting (prevents hard clipping)
            int16_t excess = buffer[i] - threshold_value;
            buffer[i] = threshold_value + (excess / 4);  // Reduce excess by 75%
        } else if (buffer[i] < -threshold_value) {
            int16_t excess = buffer[i] + threshold_value;
            buffer[i] = -threshold_value + (excess / 4);
        }
    }
}

void audio_processor_sidetone(const int16_t *mic_in, const int16_t *audio_in,
                              int16_t *output, size_t sample_count,
                              float sidetone_level, bool ptt_active)
{
    if (!mic_in || !audio_in || !output) {
        return;
    }

#if DEVICE_TYPE_PACK && SIDETONE_ENABLE
    if (ptt_active && sidetone_level > 0.0f) {
        // Mix incoming audio with sidetone (mic loopback)
        // Typical: 70% incoming + 30% sidetone
        float incoming_mix = 1.0f - sidetone_level;
        audio_processor_mix(audio_in, mic_in, output, sample_count,
                           incoming_mix, sidetone_level);
    } else {
        // No sidetone - just copy incoming audio
        memcpy(output, audio_in, sample_count * sizeof(int16_t));
    }
#else
    // Base station or sidetone disabled - just copy incoming audio
    memcpy(output, audio_in, sample_count * sizeof(int16_t));
#endif
}

float audio_processor_get_rms(const int16_t *buffer, size_t sample_count)
{
    if (!buffer || sample_count == 0) {
        return 0.0f;
    }

    int64_t sum = 0;
    for (size_t i = 0; i < sample_count; i++) {
        int32_t sample = buffer[i];
        sum += sample * sample;
    }

    float mean = (float)sum / sample_count;
    float rms = sqrtf(mean);

    // Normalize to 0.0-1.0 range
    return rms / 32768.0f;
}