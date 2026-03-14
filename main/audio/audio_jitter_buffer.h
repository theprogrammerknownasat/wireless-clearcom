/**
 * @file audio_jitter_buffer.h
 * @brief Jitter Buffer for Audio Receive Path
 *
 * Buffers decoded PCM frames to absorb WiFi timing jitter.
 * A playback task drains the buffer at a steady 20ms cadence,
 * producing smooth audio output regardless of packet arrival timing.
 */

#ifndef AUDIO_JITTER_BUFFER_H
#define AUDIO_JITTER_BUFFER_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

/**
 * @brief Initialize the jitter buffer and allocate storage
 * @return ESP_OK on success
 */
esp_err_t jitter_buffer_init(void);

/**
 * @brief Push a decoded PCM frame into the buffer
 * @param frame  Pointer to PCM samples (int16_t)
 * @param samples Number of samples in the frame
 * @return true if the frame was stored, false if the buffer was full (frame dropped)
 */
bool jitter_buffer_push(const int16_t *frame, size_t samples);

/**
 * @brief Pop the oldest PCM frame from the buffer
 * @param frame   Output buffer for PCM samples
 * @param samples Number of samples to read
 * @return true if a frame was available, false if the buffer was empty
 */
bool jitter_buffer_pop(int16_t *frame, size_t samples);

/**
 * @brief Reset the buffer (discard all queued frames)
 */
void jitter_buffer_reset(void);

/**
 * @brief Free all resources allocated by jitter_buffer_init()
 */
void jitter_buffer_deinit(void);

#endif // AUDIO_JITTER_BUFFER_H
