/**
 * @file audio_processor.h
 * @brief Audio Processing Pipeline
 *
 * Handles audio mixing, sidetone, and limiting.
 */

#ifndef AUDIO_PROCESSOR_H
#define AUDIO_PROCESSOR_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

//=============================================================================
// AUDIO PROCESSING
//=============================================================================

/**
 * @brief Initialize audio processor
 * @return ESP_OK on success
 */
esp_err_t audio_processor_init(void);

/**
 * @brief Mix two audio streams
 * @param stream1 First audio stream
 * @param stream2 Second audio stream
 * @param output Output buffer
 * @param sample_count Number of samples
 * @param mix1 Mix level for stream1 (0.0 to 1.0)
 * @param mix2 Mix level for stream2 (0.0 to 1.0)
 */
void audio_processor_mix(const int16_t *stream1, const int16_t *stream2,
                         int16_t *output, size_t sample_count,
                         float mix1, float mix2);

/**
 * @brief Apply audio limiter to prevent clipping
 * @param buffer Audio buffer (modified in-place)
 * @param sample_count Number of samples
 * @param threshold Limiter threshold (0.0 to 1.0, default 0.95)
 */
void audio_processor_limit(int16_t *buffer, size_t sample_count, float threshold);

/**
 * @brief Apply sidetone (mic to headset loopback)
 * @param mic_in Microphone input
 * @param audio_in Incoming audio (from network)
 * @param output Output to headset
 * @param sample_count Number of samples
 * @param sidetone_level Sidetone mix level (0.0 to 1.0)
 * @param ptt_active Is PTT button active?
 */
void audio_processor_sidetone(const int16_t *mic_in, const int16_t *audio_in,
                              int16_t *output, size_t sample_count,
                              float sidetone_level, bool ptt_active);

/**
 * @brief Calculate RMS level of audio signal
 * @param buffer Audio buffer
 * @param sample_count Number of samples
 * @return RMS level (0.0 to 1.0 scale)
 */
float audio_processor_get_rms(const int16_t *buffer, size_t sample_count);

#endif // AUDIO_PROCESSOR_H