/**
 * @file audio_codec.h
 * @brief WM8960 Audio Codec Driver
 *
 * I2S and I2C driver for WM8960 audio codec with simulation mode.
 * Set SIMULATE_HARDWARE=1 to test without real WM8960.
 */

#ifndef AUDIO_CODEC_H
#define AUDIO_CODEC_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

//=============================================================================
// SIMULATION MODE (Set to 0 when WM8960 hardware is connected)
//=============================================================================

#define SIMULATE_HARDWARE   1    // 1 = Generate fake audio, 0 = Real WM8960

//=============================================================================
// CODEC CONFIGURATION
//=============================================================================

typedef enum {
    CODEC_INPUT_MIC,       // Microphone input
    CODEC_INPUT_LINE       // Line input (for ClearCom party line)
} codec_input_t;

typedef enum {
    CODEC_OUTPUT_SPEAKER,  // Speaker/headphone output
    CODEC_OUTPUT_LINE      // Line output (for ClearCom party line)
} codec_output_t;

//=============================================================================
// PUBLIC FUNCTIONS
//=============================================================================

/**
 * @brief Initialize WM8960 codec
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t audio_codec_init(void);

/**
 * @brief Set input source
 * @param input Input source selection
 * @return ESP_OK on success
 */
esp_err_t audio_codec_set_input(codec_input_t input);

/**
 * @brief Set output destination
 * @param output Output destination selection
 * @return ESP_OK on success
 */
esp_err_t audio_codec_set_output(codec_output_t output);

/**
 * @brief Set input gain
 * @param gain Gain level (0-31, higher = more gain)
 * @return ESP_OK on success
 */
esp_err_t audio_codec_set_input_gain(uint8_t gain);

/**
 * @brief Set output volume
 * @param volume Volume level (0-31, higher = louder)
 * @return ESP_OK on success
 */
esp_err_t audio_codec_set_output_volume(uint8_t volume);

/**
 * @brief Read audio samples from codec (microphone/line input)
 * @param buffer Buffer to store samples
 * @param sample_count Number of samples to read
 * @return ESP_OK on success
 */
esp_err_t audio_codec_read(int16_t *buffer, size_t sample_count);

/**
 * @brief Write audio samples to codec (speaker/line output)
 * @param buffer Buffer containing samples
 * @param sample_count Number of samples to write
 * @return ESP_OK on success
 */
esp_err_t audio_codec_write(const int16_t *buffer, size_t sample_count);

/**
 * @brief Enable/disable sidetone monitoring
 * @param enable true to enable, false to disable
 * @param level Sidetone level (0.0 to 1.0)
 * @return ESP_OK on success
 */
esp_err_t audio_codec_set_sidetone(bool enable, float level);

/**
 * @brief Deinitialize codec
 */
void audio_codec_deinit(void);

#endif // AUDIO_CODEC_H