/**
 * @file test_mode_pack.c
 * @brief Belt Pack Test Mode Implementation
 *
 * Two test modes controlled by TEST_PACK_MODE in config_pack.h:
 *   0 = Mic loopback with delay (tests mic + speaker + codec)
 *   1 = 440Hz sine wave output (tests speaker output at known level)
 *
 * The sine wave is full-scale (0 dBFS, peak = 32767). If this is
 * quiet at max volume, the output gain needs increasing in codec config.
 * If it's loud, then the mic gain is the issue on loopback.
 */

#include "test_mode_pack.h"
#include "config.h"

#if DEVICE_TYPE_PACK && TEST_MODE_ENABLE

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "audio/audio_codec.h"
#include "hardware/gpio_control.h"
#include <math.h>
#include <string.h>

static const char *TAG = "TEST_MODE";

//=============================================================================
// TEST PARAMETERS
//=============================================================================

// Which test to run (set in config_pack.h, default 0)
#ifndef TEST_PACK_MODE
#define TEST_PACK_MODE          0   // 0=loopback, 1=tone
#endif

#define TEST_PACKET_INTERVAL_MS 20       // 20ms = 50Hz
#define DELAY_SECONDS           2        // 2 second delay
#define DELAY_FRAMES            (DELAY_SECONDS * 50)  // 50 frames per second
#define DELAY_BUFFER_SIZE       (DELAY_FRAMES * SAMPLES_PER_FRAME)

#define TONE_FREQUENCY_HZ       440
#define TONE_AMPLITUDE          32767    // Full scale (0 dBFS)

//=============================================================================
// PRIVATE VARIABLES
//=============================================================================

static volatile bool test_mode_running = false;
static TaskHandle_t test_task_handle = NULL;

#if (TEST_PACK_MODE == 0)
// Circular delay buffer for loopback
static int16_t *delay_buffer = NULL;
static uint32_t delay_write_idx = 0;
static uint32_t delay_read_idx = 0;
#endif

//=============================================================================
// LOOPBACK MODE (TEST_PACK_MODE == 0)
//=============================================================================

#if (TEST_PACK_MODE == 0)

static bool init_delay_buffer(void)
{
    delay_buffer = (int16_t *)malloc(DELAY_BUFFER_SIZE * sizeof(int16_t));
    if (!delay_buffer) {
        printf("ERROR: Failed to allocate delay buffer (%zu bytes)\n",
               DELAY_BUFFER_SIZE * sizeof(int16_t));
        return false;
    }

    memset(delay_buffer, 0, DELAY_BUFFER_SIZE * sizeof(int16_t));
    delay_write_idx = 0;
    delay_read_idx = 0;

    printf("Delay buffer allocated: %d samples (%.1f seconds)\n",
           DELAY_BUFFER_SIZE, (float)DELAY_BUFFER_SIZE / SAMPLE_RATE_HZ);
    return true;
}

static void test_loopback_task(void *arg)
{
    printf("\n========================================\n");
    printf("  BELT PACK LOOPBACK TEST\n");
    printf("  Mic -> %ds delay -> Headphones\n", DELAY_SECONDS);
    printf("========================================\n\n");

    int16_t input_buffer[SAMPLES_PER_FRAME];
    int16_t output_buffer[SAMPLES_PER_FRAME];
    uint32_t frame_count = 0;

    while (test_mode_running) {
        // I2S read blocks until DMA fills (~20ms) - natural pacing
        size_t samples_read = 0;
        audio_codec_read(input_buffer, SAMPLES_PER_FRAME, &samples_read);

        // Write to delay buffer
        for (size_t i = 0; i < SAMPLES_PER_FRAME; i++) {
            delay_buffer[delay_write_idx] = input_buffer[i];
            delay_write_idx = (delay_write_idx + 1) % DELAY_BUFFER_SIZE;
        }

        // Read from delay buffer
        for (size_t i = 0; i < SAMPLES_PER_FRAME; i++) {
            output_buffer[i] = delay_buffer[delay_read_idx];
            delay_read_idx = (delay_read_idx + 1) % DELAY_BUFFER_SIZE;
        }

        audio_codec_write(output_buffer, SAMPLES_PER_FRAME);
        frame_count++;

        // Status every 5 seconds (reduced from 1s to minimize UART interference)
        if (frame_count % 250 == 0) {
            int16_t peak = 0;
            for (int i = 0; i < SAMPLES_PER_FRAME; i++) {
                int16_t v = input_buffer[i] < 0 ? -input_buffer[i] : input_buffer[i];
                if (v > peak) peak = v;
            }
            float db = (peak > 0) ? 20.0f * log10f((float)peak / 32768.0f) : -99.0f;
            ESP_LOGI(TAG, "Loopback: %.0fs, mic peak=%d (%.1f dBFS)",
                     (float)frame_count / 50.0f, peak, db);
        }
    }

    printf("Loopback test stopped\n");
    vTaskDelete(NULL);
}

#endif // TEST_PACK_MODE == 0

//=============================================================================
// TONE MODE (TEST_PACK_MODE == 1)
//=============================================================================

#if (TEST_PACK_MODE == 1)

static void test_tone_task(void *arg)
{
    printf("\n========================================\n");
    printf("  BELT PACK TONE TEST\n");
    printf("  440 Hz sine wave, 0 dBFS (full scale)\n");
    printf("  Peak amplitude: %d\n", TONE_AMPLITUDE);
    printf("  If this is quiet at max volume,\n");
    printf("  the output gain needs increasing.\n");
    printf("========================================\n\n");

    int16_t tone_buffer[SAMPLES_PER_FRAME];
    uint32_t sample_index = 0;
    uint32_t frame_count = 0;

    // Pre-calculate phase increment
    // phase_inc = 2*pi*freq/sample_rate
    const float phase_inc = 2.0f * M_PI * TONE_FREQUENCY_HZ / SAMPLE_RATE_HZ;

    while (test_mode_running) {
        // Generate one frame of 440Hz sine
        for (int i = 0; i < SAMPLES_PER_FRAME; i++) {
            float sample = sinf(phase_inc * (float)sample_index);
            tone_buffer[i] = (int16_t)(sample * TONE_AMPLITUDE);
            sample_index++;
        }

        audio_codec_write(tone_buffer, SAMPLES_PER_FRAME);
        frame_count++;

        // Also read from mic so I2S RX DMA doesn't overflow
        int16_t discard[SAMPLES_PER_FRAME];
        audio_codec_read(discard, SAMPLES_PER_FRAME, NULL);

        // Status every 5 seconds
        if (frame_count % 250 == 0) {
            ESP_LOGI(TAG, "Tone output: %.0fs, %d Hz, 0 dBFS",
                     (float)frame_count / 50.0f, TONE_FREQUENCY_HZ);
        }
    }

    printf("Tone test stopped\n");
    vTaskDelete(NULL);
}

#endif // TEST_PACK_MODE == 1

//=============================================================================
// PUBLIC FUNCTIONS
//=============================================================================

esp_err_t test_mode_start(void)
{
    if (test_mode_running) {
        printf("Test mode already running\n");
        return ESP_OK;
    }

    // Force all LEDs to static state during test mode
    // GPIO toggling couples noise into the audio output path
    gpio_control_set_led(LED_STATUS, LED_OFF);
    gpio_control_set_led(LED_CALL, LED_OFF);

    // Configure codec
    audio_codec_set_input(CODEC_INPUT_MIC);
    audio_codec_set_output(CODEC_OUTPUT_SPEAKER);

    test_mode_running = true;

#if (TEST_PACK_MODE == 0)
    printf("Starting loopback test (mic -> %ds delay -> headphones)...\n", DELAY_SECONDS);
    if (!init_delay_buffer()) {
        printf("ERROR: Failed to initialize delay buffer!\n");
        test_mode_running = false;
        return ESP_FAIL;
    }
    xTaskCreate(test_loopback_task, "loopback", 8192, NULL, 5, &test_task_handle);
#elif (TEST_PACK_MODE == 1)
    printf("Starting tone test (440 Hz, 0 dBFS)...\n");
    xTaskCreate(test_tone_task, "tone_test", 8192, NULL, 5, &test_task_handle);
#else
    #error "TEST_PACK_MODE must be 0 (loopback) or 1 (tone)"
#endif

    printf("Test mode started successfully\n");
    return ESP_OK;
}

void test_mode_stop(void)
{
    if (!test_mode_running) {
        return;
    }

    ESP_LOGI(TAG, "Stopping test mode...");
    test_mode_running = false;

    if (test_task_handle) {
        vTaskDelay(pdMS_TO_TICKS(100));
        test_task_handle = NULL;
    }

#if (TEST_PACK_MODE == 0)
    if (delay_buffer) {
        free(delay_buffer);
        delay_buffer = NULL;
    }
#endif

    ESP_LOGI(TAG, "Test mode stopped");
}

#endif // DEVICE_TYPE_PACK && TEST_MODE_ENABLE
