/**
 * @file test_mode_base.c
 * @brief Base Station Test Mode
 *
 * Generates 440Hz sine wave directly to partyline output and monitors
 * partyline input levels. Optionally monitors call detection ADC.
 *
 * Enable via TEST_MODE_ENABLE=1 in config_common.h
 */

#include "test_mode_base.h"
#include "config.h"

#if DEVICE_TYPE_BASE && TEST_MODE_ENABLE

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "audio/audio_codec.h"
#include "esp_adc/adc_oneshot.h"
#include <math.h>

static const char *TAG = "TEST_BASE";

#define TEST_TONE_FREQ_HZ       440.0f
#define TEST_TONE_AMPLITUDE     1.0f
#define TEST_PACKET_INTERVAL_MS 20
#define CALL_CHECK_INTERVAL_MS  100

// Set to 1 to also monitor call detection ADC during test
#define TEST_CALL_MONITORING    0

static volatile bool test_running = false;
static TaskHandle_t audio_task_handle = NULL;
static TaskHandle_t call_task_handle = NULL;
static adc_oneshot_unit_handle_t adc_handle = NULL;

static float phase = 0.0f;
static const float phase_inc = (2.0f * M_PI * TEST_TONE_FREQ_HZ) / SAMPLE_RATE_HZ;

static void generate_tone(int16_t *buf, size_t count)
{
    for (size_t i = 0; i < count; i++) {
        buf[i] = (int16_t)(sinf(phase) * TEST_TONE_AMPLITUDE * 32767.0f);
        phase += phase_inc;
        if (phase >= 2.0f * M_PI) phase -= 2.0f * M_PI;
    }
}

static void test_audio_task(void *arg)
{
    ESP_LOGI(TAG, "TX: 440Hz tone -> partyline");
    ESP_LOGI(TAG, "RX: Monitoring partyline input");

    audio_codec_set_output_volume(31);

    int16_t tx_buf[SAMPLES_PER_FRAME];
    int16_t rx_buf[SAMPLES_PER_FRAME];
    TickType_t last_wake = xTaskGetTickCount();
    uint32_t count = 0;

    while (test_running) {
        generate_tone(tx_buf, SAMPLES_PER_FRAME);
        audio_codec_write(tx_buf, SAMPLES_PER_FRAME);

        size_t samples_read = 0;
        esp_err_t ret = audio_codec_read(rx_buf, SAMPLES_PER_FRAME, &samples_read);

        count++;

        // Log every second
        if (count % 50 == 0 && ret == ESP_OK) {
            int16_t rx_max = 0, rx_min = 0;
            int32_t rx_sum = 0;
            for (int i = 0; i < SAMPLES_PER_FRAME; i++) {
                if (rx_buf[i] > rx_max) rx_max = rx_buf[i];
                if (rx_buf[i] < rx_min) rx_min = rx_buf[i];
                rx_sum += rx_buf[i];
            }
            int16_t peak = (rx_max > -rx_min) ? rx_max : -rx_min;
            float db = (peak > 0) ? 20.0f * log10f((float)peak / 32768.0f) : -99.0f;
            ESP_LOGI(TAG, "[%.0fs] TX: %lu frames | RX: peak=%d (%.1fdB) dc=%d",
                     count / 50.0f, (unsigned long)count, peak, db,
                     (int16_t)(rx_sum / SAMPLES_PER_FRAME));
        }

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(TEST_PACKET_INTERVAL_MS));
    }

    vTaskDelete(NULL);
}

#if TEST_CALL_MONITORING
static void test_call_task(void *arg)
{
    ESP_LOGI(TAG, "Call monitor: GPIO%d, threshold %.1fV", CALL_RX_PIN, CALL_VOLTAGE_THRESHOLD);

    bool call_active = false;

    while (test_running) {
        int adc_value = 0;
        if (adc_oneshot_read(adc_handle, CALL_RX_ADC_CHANNEL, &adc_value) == ESP_OK) {
            float voltage = (adc_value / 4095.0f) * 3.3f;
            bool detected = (voltage > CALL_VOLTAGE_THRESHOLD);

            if (detected && !call_active) {
                call_active = true;
                ESP_LOGI(TAG, "CALL detected (%.2fV)", voltage);
            } else if (!detected && call_active) {
                call_active = false;
                ESP_LOGI(TAG, "Call ended (%.2fV)", voltage);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(CALL_CHECK_INTERVAL_MS));
    }

    vTaskDelete(NULL);
}

static esp_err_t init_call_adc(void)
{
    adc_oneshot_unit_init_cfg_t cfg = { .unit_id = ADC_UNIT_1 };
    esp_err_t ret = adc_oneshot_new_unit(&cfg, &adc_handle);
    if (ret != ESP_OK) return ret;

    adc_oneshot_chan_cfg_t ch_cfg = {
        .bitwidth = ADC_BITWIDTH_12,
        .atten = ADC_ATTEN_DB_12,
    };
    return adc_oneshot_config_channel(adc_handle, CALL_RX_ADC_CHANNEL, &ch_cfg);
}
#endif

esp_err_t test_mode_start(void)
{
    if (test_running) return ESP_OK;

    ESP_LOGI(TAG, "=== BASE TEST MODE ===");

#if TEST_CALL_MONITORING
    if (init_call_adc() != ESP_OK) {
        ESP_LOGE(TAG, "Call ADC init failed");
        return ESP_FAIL;
    }
#endif

    test_running = true;
    xTaskCreate(test_audio_task, "test_audio", 4096, NULL, 5, &audio_task_handle);

#if TEST_CALL_MONITORING
    xTaskCreate(test_call_task, "test_call", 4096, NULL, 4, &call_task_handle);
#endif

    return ESP_OK;
}

void test_mode_stop(void)
{
    if (!test_running) return;
    test_running = false;
    vTaskDelay(pdMS_TO_TICKS(200));

    if (adc_handle) {
        adc_oneshot_del_unit(adc_handle);
        adc_handle = NULL;
    }

    ESP_LOGI(TAG, "Test mode stopped");
}

bool test_mode_is_running(void)
{
    return test_running;
}

#endif // DEVICE_TYPE_BASE && TEST_MODE_ENABLE
