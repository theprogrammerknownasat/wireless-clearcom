/**
* @file audio_tones.h
 * @brief Audio Tone Generation
 *
 * Generates sine wave tones for testing and user feedback.
 */

#ifndef AUDIO_TONES_H
#define AUDIO_TONES_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

//=============================================================================
// TONE GENERATION
//=============================================================================

/**
 * @brief Generate a sine wave tone
 * @param buffer Output buffer for PCM samples
 * @param sample_count Number of samples to generate
 * @param frequency_hz Tone frequency in Hz
 * @param amplitude Peak amplitude (0.0 to 1.0)
 * @param phase Pointer to phase accumulator (maintains continuity)
 */
void audio_tones_generate_sine(int16_t *buffer, size_t sample_count,
                               float frequency_hz, float amplitude,
                               float *phase);

/**
 * @brief Play a tone (non-blocking start)
 * @param frequency_hz Tone frequency
 * @param duration_ms Duration in milliseconds
 * @param amplitude Peak amplitude (0.0 to 1.0)
 * @return ESP_OK on success
 *
 * Note: Requires tone playback task to be running
 */
esp_err_t audio_tones_play(uint16_t frequency_hz, uint16_t duration_ms, float amplitude);

/**
 * @brief Stop currently playing tone
 */
void audio_tones_stop(void);

/**
 * @brief Check if tone is currently playing
 * @return true if tone active, false otherwise
 */
bool audio_tones_is_playing(void);

/**
 * @brief Initialize tone generator
 * @return ESP_OK on success
 */
esp_err_t audio_tones_init(void);

#endif // AUDIO_TONES_H