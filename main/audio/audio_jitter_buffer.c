/**
 * @file audio_jitter_buffer.c
 * @brief Jitter Buffer Implementation
 *
 * Simple ring buffer of decoded PCM frames.  The UDP receive callback
 * pushes frames in; a dedicated playback task pops them out at a
 * steady 20 ms cadence and writes to I2S.
 */

#include "audio_jitter_buffer.h"
#include "../config.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"

static const char *TAG = "JBUF";

//=============================================================================
// RING BUFFER STATE
//=============================================================================

static int16_t *ring_buf = NULL;          // flat array: JITTER_BUFFER_FRAMES * SAMPLES_PER_FRAME
static size_t   frame_capacity = 0;       // JITTER_BUFFER_FRAMES
static size_t   head = 0;                 // next write slot
static size_t   tail = 0;                 // next read slot
static size_t   count = 0;               // frames currently stored
static SemaphoreHandle_t mutex = NULL;
static bool     initialized = false;

//=============================================================================
// PUBLIC FUNCTIONS
//=============================================================================

esp_err_t jitter_buffer_init(void)
{
    if (initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_OK;
    }

    frame_capacity = JITTER_BUFFER_FRAMES;

    size_t total_samples = frame_capacity * SAMPLES_PER_FRAME;
    ring_buf = heap_caps_malloc(total_samples * sizeof(int16_t), MALLOC_CAP_INTERNAL);
    if (!ring_buf) {
        ESP_LOGE(TAG, "Failed to allocate jitter buffer (%u bytes)",
                 (unsigned)(total_samples * sizeof(int16_t)));
        return ESP_ERR_NO_MEM;
    }
    memset(ring_buf, 0, total_samples * sizeof(int16_t));

    mutex = xSemaphoreCreateMutex();
    if (!mutex) {
        free(ring_buf);
        ring_buf = NULL;
        return ESP_ERR_NO_MEM;
    }

    head = 0;
    tail = 0;
    count = 0;
    initialized = true;

    ESP_LOGI(TAG, "Jitter buffer ready: %u frames (%u ms)",
             (unsigned)frame_capacity,
             (unsigned)(frame_capacity * FRAME_SIZE_MS));
    return ESP_OK;
}

bool jitter_buffer_push(const int16_t *frame, size_t samples)
{
    if (!initialized || !frame) return false;

    size_t copy_samples = (samples < SAMPLES_PER_FRAME) ? samples : SAMPLES_PER_FRAME;

    xSemaphoreTake(mutex, portMAX_DELAY);

    if (count >= frame_capacity) {
        xSemaphoreGive(mutex);
        ESP_LOGW(TAG, "Buffer full - frame dropped");
        return false;
    }

    int16_t *dst = &ring_buf[head * SAMPLES_PER_FRAME];
    memcpy(dst, frame, copy_samples * sizeof(int16_t));

    // Zero-pad if the pushed frame is shorter than SAMPLES_PER_FRAME
    if (copy_samples < SAMPLES_PER_FRAME) {
        memset(&dst[copy_samples], 0,
               (SAMPLES_PER_FRAME - copy_samples) * sizeof(int16_t));
    }

    head = (head + 1) % frame_capacity;
    count++;

    xSemaphoreGive(mutex);
    return true;
}

bool jitter_buffer_pop(int16_t *frame, size_t samples)
{
    if (!initialized || !frame) return false;

    size_t copy_samples = (samples < SAMPLES_PER_FRAME) ? samples : SAMPLES_PER_FRAME;

    xSemaphoreTake(mutex, portMAX_DELAY);

    if (count == 0) {
        xSemaphoreGive(mutex);
        return false;
    }

    const int16_t *src = &ring_buf[tail * SAMPLES_PER_FRAME];
    memcpy(frame, src, copy_samples * sizeof(int16_t));

    tail = (tail + 1) % frame_capacity;
    count--;

    xSemaphoreGive(mutex);
    return true;
}

void jitter_buffer_reset(void)
{
    if (!initialized) return;

    xSemaphoreTake(mutex, portMAX_DELAY);
    head = 0;
    tail = 0;
    count = 0;
    xSemaphoreGive(mutex);

    ESP_LOGI(TAG, "Buffer reset");
}

void jitter_buffer_deinit(void)
{
    if (!initialized) return;

    initialized = false;

    if (mutex) {
        vSemaphoreDelete(mutex);
        mutex = NULL;
    }
    if (ring_buf) {
        free(ring_buf);
        ring_buf = NULL;
    }

    head = 0;
    tail = 0;
    count = 0;
    frame_capacity = 0;

    ESP_LOGI(TAG, "Jitter buffer freed");
}
