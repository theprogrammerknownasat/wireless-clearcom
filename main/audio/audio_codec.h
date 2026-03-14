/**
 * @file audio_codec.h
 * @brief WM8960 Audio Codec Driver
 *
 * I2S and I2C driver for WM8960 audio codec.
 * Device-specific configuration selected at compile time.
 */

#ifndef AUDIO_CODEC_H
#define AUDIO_CODEC_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

//=============================================================================
// CODEC CONFIGURATION
//=============================================================================

typedef enum {
    CODEC_INPUT_MIC,
    CODEC_INPUT_LINE
} codec_input_t;

typedef enum {
    CODEC_OUTPUT_SPEAKER,
    CODEC_OUTPUT_LINE
} codec_output_t;

//=============================================================================
// PUBLIC FUNCTIONS
//=============================================================================

esp_err_t audio_codec_init(void);
esp_err_t audio_codec_set_input(codec_input_t input);
esp_err_t audio_codec_set_output(codec_output_t output);
esp_err_t audio_codec_set_input_gain(uint8_t gain);
esp_err_t audio_codec_set_output_volume(uint8_t volume);
esp_err_t audio_codec_read(int16_t *buffer, size_t sample_count, size_t *samples_read);
esp_err_t audio_codec_write(const int16_t *buffer, size_t sample_count);
esp_err_t audio_codec_set_sidetone(bool enable, float level);
void audio_codec_deinit(void);

/**
 * @brief Check if audio codec was successfully initialized
 * @return true if initialized, false otherwise
 */
bool audio_codec_is_initialized(void);

#endif // AUDIO_CODEC_H
