/**
 * @file test_mode_pack.c
 * @brief Belt Pack Test Mode Implementation
 * 
 * Generates 440Hz sine wave tone and outputs to headphones
 * to verify WM8960 is working correctly.
 */

#include "test_mode_pack.h"
#include "config.h"

#if DEVICE_TYPE_PACK && TEST_MODE_ENABLE

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "audio/audio_codec.h"
#include "audio/audio_opus.h"
#include <math.h>

static const char *TAG = "TEST_MODE";

//=============================================================================
// TEST PARAMETERS
//=============================================================================

#define TEST_PACKET_INTERVAL_MS 20       // 20ms = 50Hz
#define DELAY_SECONDS           2        // 2 second delay
#define DELAY_FRAMES            (DELAY_SECONDS * 50)  // 50 frames per second
#define DELAY_BUFFER_SIZE       (DELAY_FRAMES * SAMPLES_PER_FRAME)

//=============================================================================
// PRIVATE VARIABLES
//=============================================================================

static bool test_mode_running = false;
static TaskHandle_t test_task_handle = NULL;

// Circular delay buffer for loopback
static int16_t *delay_buffer = NULL;
static uint32_t delay_write_idx = 0;
static uint32_t delay_read_idx = 0;

//=============================================================================
// PRIVATE FUNCTIONS
//=============================================================================

/**
 * @brief Initialize the delay buffer
 */
static bool init_delay_buffer(void)
{
    delay_buffer = (int16_t *)malloc(DELAY_BUFFER_SIZE * sizeof(int16_t));
    if (!delay_buffer) {
        printf("ERROR: Failed to allocate delay buffer (%d bytes)\n",
               DELAY_BUFFER_SIZE * sizeof(int16_t));
        return false;
    }

    // Zero the buffer
    memset(delay_buffer, 0, DELAY_BUFFER_SIZE * sizeof(int16_t));

    delay_write_idx = 0;
    delay_read_idx = 0;

    printf("Delay buffer allocated: %d samples (%.1f seconds)\n",
           DELAY_BUFFER_SIZE, (float)DELAY_BUFFER_SIZE / SAMPLE_RATE_HZ);

    return true;
}

/**
 * @brief Write audio frame to delay buffer
 */
static void write_to_delay_buffer(const int16_t *buffer, size_t sample_count)
{
    for (size_t i = 0; i < sample_count; i++) {
        delay_buffer[delay_write_idx] = buffer[i];
        delay_write_idx = (delay_write_idx + 1) % DELAY_BUFFER_SIZE;
    }
}

/**
 * @brief Read audio frame from delay buffer
 */
static void read_from_delay_buffer(int16_t *buffer, size_t sample_count)
{
    for (size_t i = 0; i < sample_count; i++) {
        buffer[i] = delay_buffer[delay_read_idx];
        delay_read_idx = (delay_read_idx + 1) % DELAY_BUFFER_SIZE;
    }
}

/**
 * @brief Test audio task - microphone loopback with 2 second delay
 */
static void test_audio_task(void *arg)
{
    printf("\n\n");
    printf("========================================\n");
    printf("  BELT PACK LOOPBACK TEST MODE\n");
    printf("========================================\n");
    printf("🎤 Microphone → 2 second delay → 🎧 Headphones\n");
    printf("Speak into microphone, hear yourself 2s later\n");
    printf("========================================\n\n");

    int16_t input_buffer[SAMPLES_PER_FRAME];
    int16_t output_buffer[SAMPLES_PER_FRAME];

    TickType_t last_wake_time = xTaskGetTickCount();
    uint32_t frame_count = 0;
    bool first_output = false;

    while (test_mode_running) {
        // Read from microphone
        size_t samples_read = 0;
        esp_err_t ret = audio_codec_read(input_buffer, SAMPLES_PER_FRAME, &samples_read);

        if (ret != ESP_OK || samples_read != SAMPLES_PER_FRAME) {
            printf("WARNING: Read %zu samples (expected %d), ret=%d\n",
                   samples_read, SAMPLES_PER_FRAME, ret);
        }

        // Write input to delay buffer
        write_to_delay_buffer(input_buffer, SAMPLES_PER_FRAME);

        // Read delayed audio from buffer
        read_from_delay_buffer(output_buffer, SAMPLES_PER_FRAME);

        // Output delayed audio to headphones
        audio_codec_write(output_buffer, SAMPLES_PER_FRAME);

        frame_count++;

        // Log first output frame (after 2 second delay is filled)
        if (!first_output && frame_count >= DELAY_FRAMES) {
            printf("✓ Delay buffer filled! Loopback active.\n");
            printf("First output frame: samples=%d, first=%d, mid=%d, last=%d\n",
                   SAMPLES_PER_FRAME, output_buffer[0],
                   output_buffer[SAMPLES_PER_FRAME/2],
                   output_buffer[SAMPLES_PER_FRAME-1]);
            first_output = true;
        }

        // Status every second
        if (frame_count % 50 == 0) {
            float seconds = (float)frame_count / 50.0f;
            ESP_LOGI(TAG, "Loopback running: %.1fs (frame %lu)", seconds, frame_count);

            // Show input level
            int16_t max_input = 0;
            int16_t min_input = 0;
            for (int i = 0; i < SAMPLES_PER_FRAME; i++) {
                if (input_buffer[i] > max_input) max_input = input_buffer[i];
                if (input_buffer[i] < min_input) min_input = input_buffer[i];
            }
            int16_t peak = (max_input > abs(min_input)) ? max_input : abs(min_input);
            float input_db = (peak > 0) ? 20.0f * log10f((float)peak / 32768.0f) : -INFINITY;

            printf("  Input: peak=%d, min=%d, max=%d (%.1f dB)\n",
                   peak, min_input, max_input, input_db);
            printf("  First 8 samples: %d, %d, %d, %d, %d, %d, %d, %d\n",
                   input_buffer[0], input_buffer[1], input_buffer[2], input_buffer[3],
                   input_buffer[4], input_buffer[5], input_buffer[6], input_buffer[7]);
        }

        // Wait for next frame
        vTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(TEST_PACKET_INTERVAL_MS));
    }

    printf("Loopback test stopped\n");
    vTaskDelete(NULL);
}

//=============================================================================
// PUBLIC FUNCTIONS
//=============================================================================

esp_err_t test_mode_start(void)
{
    printf("\n\n========================================\n");
    printf(">>>>>> PACK test_mode_start() ENTERED <<<<<<\n");
    printf(">>>>>> THIS IS THE PACK VERSION <<<<<<\n");
    printf("========================================\n\n");

    if (test_mode_running) {
        printf("Test mode already running\n");
        return ESP_OK;
    }

    printf("Starting belt pack loopback test mode...\n");

    // Allocate delay buffer
    if (!init_delay_buffer()) {
        printf("ERROR: Failed to initialize delay buffer!\n");
        return ESP_FAIL;
    }

    // Configure codec for microphone input and headphone output
    audio_codec_set_input(CODEC_INPUT_MIC);
    audio_codec_set_output(CODEC_OUTPUT_SPEAKER);  // SPEAKER = Headphones on pack

    test_mode_running = true;

    // Start loopback task
    printf("About to create loopback task...\n");
    xTaskCreate(test_audio_task, "loopback", 8192, NULL, 5, &test_task_handle);
    printf("Task created, handle=%p\n", test_task_handle);

    printf("Loopback test mode started successfully\n");
    printf("🎤 Speak into microphone - you'll hear yourself after 2 second delay\n\n");

    return ESP_OK;
}

void test_mode_stop(void)
{
    if (!test_mode_running) {
        return;
    }

    ESP_LOGI(TAG, "Stopping test mode...");
    test_mode_running = false;

    // Wait for task to exit
    if (test_task_handle) {
        vTaskDelay(pdMS_TO_TICKS(100));
        test_task_handle = NULL;
    }

    // Free delay buffer
    if (delay_buffer) {
        free(delay_buffer);
        delay_buffer = NULL;
        printf("Delay buffer freed\n");
    }
    
    ESP_LOGI(TAG, "Test mode stopped");
}

#endif // DEVICE_TYPE_BASE && TEST_MODE_ENABLE