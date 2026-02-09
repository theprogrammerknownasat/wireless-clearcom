/**
 * @file audio_opus.c
 * @brief Opus Codec Wrapper Implementation
 */

#include "audio_opus.h"
#include "../config.h"
#include "opus.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <string.h>

static const char *TAG = "OPUS";

//=============================================================================
// PRIVATE VARIABLES
//=============================================================================

static OpusEncoder *encoder = NULL;
static OpusDecoder *decoder = NULL;
static bool initialized = false;

// Statistics
static int64_t total_encode_time_us = 0;
static uint32_t total_frames_encoded = 0;

//=============================================================================
// PUBLIC FUNCTIONS
//=============================================================================

esp_err_t audio_opus_init(void)
{
    if (initialized) {
        ESP_LOGW(TAG, "Opus already initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing Opus codec...");
    ESP_LOGI(TAG, "Sample rate: %d Hz", SAMPLE_RATE_HZ);
    ESP_LOGI(TAG, "Frame size: %d ms (%d samples)", FRAME_SIZE_MS, SAMPLES_PER_FRAME);
    ESP_LOGI(TAG, "Bitrate: %d bps", OPUS_BITRATE);

    int error;

    // Create encoder
    encoder = opus_encoder_create(SAMPLE_RATE_HZ, 1, OPUS_APPLICATION_VOIP, &error);
    if (error != OPUS_OK || !encoder) {
        ESP_LOGE(TAG, "Failed to create encoder: %s", opus_strerror(error));
        return ESP_FAIL;
    }

    // Configure encoder for low-latency voice
    opus_encoder_ctl(encoder, OPUS_SET_BITRATE(OPUS_BITRATE));
    opus_encoder_ctl(encoder, OPUS_SET_VBR(0));  // Constant bitrate
    opus_encoder_ctl(encoder, OPUS_SET_COMPLEXITY(OPUS_COMPLEXITY));
    opus_encoder_ctl(encoder, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
    opus_encoder_ctl(encoder, OPUS_SET_DTX(0));  // Disable discontinuous transmission

    ESP_LOGI(TAG, "Encoder created successfully");

    // Create decoder
    decoder = opus_decoder_create(SAMPLE_RATE_HZ, 1, &error);
    if (error != OPUS_OK || !decoder) {
        ESP_LOGE(TAG, "Failed to create decoder: %s", opus_strerror(error));
        opus_encoder_destroy(encoder);
        encoder = NULL;
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Decoder created successfully");

    // Reset statistics
    total_encode_time_us = 0;
    total_frames_encoded = 0;

    initialized = true;
    ESP_LOGI(TAG, "Opus initialization complete");

    return ESP_OK;
}

int audio_opus_encode(const int16_t *pcm_in, int frame_size,
                      uint8_t *opus_out, int max_size)
{
    if (!initialized || !encoder) {
        ESP_LOGE(TAG, "Encoder not initialized");
        return -1;
    }

    if (!pcm_in || !opus_out) {
        ESP_LOGE(TAG, "Invalid parameters");
        return -1;
    }

    // Measure encode time
    int64_t start = esp_timer_get_time();

    int encoded_bytes = opus_encode(encoder, pcm_in, frame_size,
                                    opus_out, max_size);

    int64_t encode_time = esp_timer_get_time() - start;

    if (encoded_bytes < 0) {
        ESP_LOGE(TAG, "Encode error: %s", opus_strerror(encoded_bytes));
        return encoded_bytes;
    }

    // Update statistics
    total_encode_time_us += encode_time;
    total_frames_encoded++;

    ESP_LOGD(TAG, "Encoded %d samples -> %d bytes (%.2f ms)",
             frame_size, encoded_bytes, encode_time / 1000.0f);

    return encoded_bytes;
}

int audio_opus_decode(const uint8_t *opus_in, int opus_size,
                      int16_t *pcm_out, int frame_size, int use_fec)
{
    if (!initialized || !decoder) {
        ESP_LOGE(TAG, "Decoder not initialized");
        return -1;
    }

    if (!pcm_out) {
        ESP_LOGE(TAG, "Invalid parameters");
        return -1;
    }

    int decoded_samples;

    if (opus_in == NULL || opus_size == 0) {
        // Packet loss - use FEC or PLC
        ESP_LOGD(TAG, "Packet loss - using PLC");
        decoded_samples = opus_decode(decoder, NULL, 0, pcm_out, frame_size, use_fec);
    } else {
        // Normal decode
        decoded_samples = opus_decode(decoder, opus_in, opus_size,
                                     pcm_out, frame_size, use_fec);
    }

    if (decoded_samples < 0) {
        ESP_LOGE(TAG, "Decode error: %s", opus_strerror(decoded_samples));
        return decoded_samples;
    }

    ESP_LOGD(TAG, "Decoded %d bytes -> %d samples", opus_size, decoded_samples);

    return decoded_samples;
}

void audio_opus_get_stats(float *avg_encode_time_ms, uint32_t *total_frames)
{
    if (!initialized) {
        if (avg_encode_time_ms) *avg_encode_time_ms = 0.0f;
        if (total_frames) *total_frames = 0;
        return;
    }

    if (avg_encode_time_ms) {
        if (total_frames_encoded > 0) {
            *avg_encode_time_ms = (float)total_encode_time_us / total_frames_encoded / 1000.0f;
        } else {
            *avg_encode_time_ms = 0.0f;
        }
    }

    if (total_frames) {
        *total_frames = total_frames_encoded;
    }
}

void audio_opus_reset_stats(void)
{
    total_encode_time_us = 0;
    total_frames_encoded = 0;
}

void audio_opus_deinit(void)
{
    if (!initialized) {
        return;
    }

    ESP_LOGI(TAG, "Destroying Opus codec");

    if (encoder) {
        opus_encoder_destroy(encoder);
        encoder = NULL;
    }

    if (decoder) {
        opus_decoder_destroy(decoder);
        decoder = NULL;
    }

    initialized = false;
    ESP_LOGI(TAG, "Opus deinitialized");
}