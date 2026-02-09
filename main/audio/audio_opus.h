/**
 * @file audio_opus.h
 * @brief Opus Codec Wrapper for ClearCom Wireless
 *
 * Provides simple encode/decode interface for Opus audio compression.
 */

#ifndef AUDIO_OPUS_H
#define AUDIO_OPUS_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

//=============================================================================
// OPUS CONFIGURATION
//=============================================================================

// Maximum encoded packet size (bytes)
// Typical: 60-80 bytes at 24kbps for 20ms frames
#define OPUS_MAX_PACKET_SIZE    256

//=============================================================================
// PUBLIC FUNCTIONS
//=============================================================================

/**
 * @brief Initialize Opus encoder and decoder
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t audio_opus_init(void);

/**
 * @brief Encode PCM audio to Opus
 * @param pcm_in Pointer to PCM samples (16-bit signed)
 * @param frame_size Number of samples per channel
 * @param opus_out Buffer for encoded data
 * @param max_size Maximum size of output buffer
 * @return Number of bytes written, or negative error code
 */
int audio_opus_encode(const int16_t *pcm_in, int frame_size,
                      uint8_t *opus_out, int max_size);

/**
 * @brief Decode Opus to PCM audio
 * @param opus_in Pointer to Opus encoded data
 * @param opus_size Size of encoded data in bytes
 * @param pcm_out Buffer for decoded PCM samples
 * @param frame_size Maximum number of samples per channel
 * @param use_fec Use forward error correction (0 or 1)
 * @return Number of samples decoded, or negative error code
 */
int audio_opus_decode(const uint8_t *opus_in, int opus_size,
                      int16_t *pcm_out, int frame_size, int use_fec);

/**
 * @brief Get encoder statistics (for monitoring)
 * @param avg_encode_time_ms Average encode time in milliseconds
 * @param total_frames_encoded Total frames encoded since init
 */
void audio_opus_get_stats(float *avg_encode_time_ms, uint32_t *total_frames_encoded);

/**
 * @brief Reset statistics
 */
void audio_opus_reset_stats(void);

/**
 * @brief Destroy encoder and decoder (cleanup)
 */
void audio_opus_deinit(void);

#endif // AUDIO_OPUS_H