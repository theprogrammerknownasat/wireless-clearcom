/**
 * @file audio_codec.c
 * @brief WM8960 Audio Codec Driver
 *
 * Handles I2C register configuration and I2S audio data transfer
 * for the WM8960 codec. Device-specific register values are selected
 * at compile time based on DEVICE_TYPE_BASE / DEVICE_TYPE_PACK.
 */

#include "audio_codec.h"
#include "../config.h"
#include "esp_log.h"
#include "driver/i2s_std.h"
#include "driver/i2c.h"
#include <string.h>

static const char *TAG = "CODEC";

//=============================================================================
// WM8960 REGISTER ADDRESSES
//=============================================================================

#define WM8960_I2C_ADDR         0x1A

#define WM8960_REG_LINVOL       0x00
#define WM8960_REG_RINVOL       0x01
#define WM8960_REG_LOUT1        0x02
#define WM8960_REG_ROUT1        0x03
#define WM8960_REG_CLOCK1       0x04
#define WM8960_REG_DACCTL1      0x05
#define WM8960_REG_IFACE1       0x07
#define WM8960_REG_LDACVOL      0x0A
#define WM8960_REG_RDACVOL      0x0B
#define WM8960_REG_RESET        0x0F
#define WM8960_REG_LADCVOL      0x15
#define WM8960_REG_RADCVOL      0x16
#define WM8960_REG_ADDCTL1      0x17
#define WM8960_REG_ADDCTL2      0x18
#define WM8960_REG_POWER1       0x19
#define WM8960_REG_POWER2       0x1A
#define WM8960_REG_LINPATH      0x20
#define WM8960_REG_RINPATH      0x21
#define WM8960_REG_LOUTMIX      0x22
#define WM8960_REG_ROUTMIX      0x25
#define WM8960_REG_LOUT2        0x28
#define WM8960_REG_ROUT2        0x29
#define WM8960_REG_POWER3       0x2F
#define WM8960_REG_LINBOOST     0x2B
#define WM8960_REG_RINBOOST     0x2C
#define WM8960_REG_ADDCTL4      0x30
#define WM8960_REG_CLASSD1      0x31

//=============================================================================
// PRIVATE VARIABLES
//=============================================================================

static bool initialized = false;
static i2s_chan_handle_t tx_handle = NULL;
static i2s_chan_handle_t rx_handle = NULL;

//=============================================================================
// I2C COMMUNICATION
//=============================================================================

static esp_err_t wm8960_write_reg(uint8_t reg, uint16_t value)
{
    uint8_t data[2];
    data[0] = (reg << 1) | ((value >> 8) & 0x01);
    data[1] = value & 0xFF;

    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (WM8960_I2C_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write(cmd, data, 2, true);
    i2c_master_stop(cmd);

    esp_err_t ret = i2c_master_cmd_begin(I2C_NUM_0, cmd, pdMS_TO_TICKS(1000));
    i2c_cmd_link_delete(cmd);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C write failed: reg=0x%02X val=0x%03X err=%d", reg, value, ret);
    }

    return ret;
}

static esp_err_t wm8960_init_i2c(void)
{
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_SDA_PIN,
        .scl_io_num = I2C_SCL_PIN,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 100000,
    };

    esp_err_t ret = i2c_param_config(I2C_NUM_0, &conf);
    if (ret != ESP_OK) return ret;

    return i2c_driver_install(I2C_NUM_0, conf.mode, 0, 0, 0);
}

//=============================================================================
// WM8960 REGISTER CONFIGURATION
//=============================================================================

static esp_err_t wm8960_configure(void)
{
    ESP_LOGI(TAG, "Configuring WM8960...");

    // Reset
    if (wm8960_write_reg(WM8960_REG_RESET, 0x000) != ESP_OK) {
        ESP_LOGE(TAG, "WM8960 not responding");
        return ESP_FAIL;
    }
    vTaskDelay(pdMS_TO_TICKS(100));

    // --- Power ---
    // POWER1: VMIDSEL=50k divider + VREF + AINL
    if (wm8960_write_reg(WM8960_REG_POWER1, 0x1C0) != ESP_OK) return ESP_FAIL;

#if DEVICE_TYPE_BASE
    // Base: DACs + LOUT1/ROUT1 + SPK, no PLL (MCLK is direct from ESP32)
    if (wm8960_write_reg(WM8960_REG_POWER2, 0x0F8) != ESP_OK) return ESP_FAIL;
#else
    // Pack: DACs + LOUT1/ROUT1 + SPKL/SPKR + PLL
    if (wm8960_write_reg(WM8960_REG_POWER2, 0x1F8) != ESP_OK) return ESP_FAIL;
#endif

    // POWER3: Output mixers enabled
    if (wm8960_write_reg(WM8960_REG_POWER3, 0x00C) != ESP_OK) return ESP_FAIL;

    // --- Clock & Interface ---
    if (wm8960_write_reg(WM8960_REG_CLOCK1, 0x000) != ESP_OK) return ESP_FAIL;
    if (wm8960_write_reg(WM8960_REG_IFACE1, 0x002) != ESP_OK) return ESP_FAIL;

    // --- DAC ---
    if (wm8960_write_reg(WM8960_REG_DACCTL1, 0x000) != ESP_OK) return ESP_FAIL;
    if (wm8960_write_reg(WM8960_REG_LDACVOL, 0x1FF) != ESP_OK) return ESP_FAIL;
    if (wm8960_write_reg(WM8960_REG_RDACVOL, 0x1FF) != ESP_OK) return ESP_FAIL;

    // --- Output Mixers ---
#if DEVICE_TYPE_BASE
    // Base: DAC only to output mixer (bit 8 = LD2LO/RD2RO)
    // Do NOT enable bit 7 (LI2LO) - LINPUT3 is floating
    if (wm8960_write_reg(WM8960_REG_LOUTMIX, 0x100) != ESP_OK) return ESP_FAIL;
    if (wm8960_write_reg(WM8960_REG_ROUTMIX, 0x100) != ESP_OK) return ESP_FAIL;
#else
    // Pack: DAC + bypass input to output (for sidetone path)
    if (wm8960_write_reg(WM8960_REG_LOUTMIX, 0x180) != ESP_OK) return ESP_FAIL;
    if (wm8960_write_reg(WM8960_REG_ROUTMIX, 0x180) != ESP_OK) return ESP_FAIL;
#endif

    // --- Output Volumes ---
#if DEVICE_TYPE_BASE
    // Base: LOUT1/ROUT1 drive transformer for partyline - max volume
    if (wm8960_write_reg(WM8960_REG_LOUT1, 0x17F) != ESP_OK) return ESP_FAIL;
    if (wm8960_write_reg(WM8960_REG_ROUT1, 0x17F) != ESP_OK) return ESP_FAIL;
    // LOUT2/ROUT2 also max (differential output through transformer)
    if (wm8960_write_reg(WM8960_REG_LOUT2, 0x1FF) != ESP_OK) return ESP_FAIL;
    if (wm8960_write_reg(WM8960_REG_ROUT2, 0x1FF) != ESP_OK) return ESP_FAIL;
    // Class D disabled - using analog outputs for transformer
    if (wm8960_write_reg(WM8960_REG_CLASSD1, 0x000) != ESP_OK) return ESP_FAIL;
#else
    // Pack: Headphone output volumes
    if (wm8960_write_reg(WM8960_REG_LOUT1, 0x161) != ESP_OK) return ESP_FAIL;
    if (wm8960_write_reg(WM8960_REG_ROUT1, 0x161) != ESP_OK) return ESP_FAIL;
    // Speaker output volumes
    if (wm8960_write_reg(WM8960_REG_LOUT2, 0x177) != ESP_OK) return ESP_FAIL;
    if (wm8960_write_reg(WM8960_REG_ROUT2, 0x177) != ESP_OK) return ESP_FAIL;
    // Class D enabled for speaker
    if (wm8960_write_reg(WM8960_REG_CLASSD1, 0x0F7) != ESP_OK) return ESP_FAIL;
#endif

    // --- Jack Detect: DISABLED for both devices ---
    // Base has no headphone jack (transformer output)
    // Pack doesn't need auto-mute behavior
    if (wm8960_write_reg(WM8960_REG_ADDCTL2, 0x000) != ESP_OK) return ESP_FAIL;
    if (wm8960_write_reg(WM8960_REG_ADDCTL4, 0x000) != ESP_OK) return ESP_FAIL;

    // --- ADC Input Configuration ---
    // Enable both ADCs and input mixers
    if (wm8960_write_reg(WM8960_REG_POWER1, 0x1FE) != ESP_OK) return ESP_FAIL;
    if (wm8960_write_reg(WM8960_REG_POWER3, 0x03C) != ESP_OK) return ESP_FAIL;

#if DEVICE_TYPE_BASE
    // Base: Differential input from partyline transformer
    // LINPUT1 inverting + LINPUT2 non-inverting, PGA to boost, +20dB
    if (wm8960_write_reg(WM8960_REG_LINPATH, 0x158) != ESP_OK) return ESP_FAIL;
    if (wm8960_write_reg(WM8960_REG_RINPATH, 0x158) != ESP_OK) return ESP_FAIL;
#else
    // Pack: Single-ended mic input on LINPUT1
    // LMN1 + LMIC2B + 20dB boost
    if (wm8960_write_reg(WM8960_REG_LINPATH, 0x138) != ESP_OK) return ESP_FAIL;
    if (wm8960_write_reg(WM8960_REG_RINPATH, 0x138) != ESP_OK) return ESP_FAIL;
#endif

    // Input boost: disable direct LINPUT2/LINPUT3 paths (use PGA path only)
    if (wm8960_write_reg(WM8960_REG_LINBOOST, 0x000) != ESP_OK) return ESP_FAIL;
    if (wm8960_write_reg(WM8960_REG_RINBOOST, 0x000) != ESP_OK) return ESP_FAIL;

    // ADC volumes: 0dB
    if (wm8960_write_reg(WM8960_REG_LADCVOL, 0x1C3) != ESP_OK) return ESP_FAIL;
    if (wm8960_write_reg(WM8960_REG_RADCVOL, 0x1C3) != ESP_OK) return ESP_FAIL;

    // Input PGA volumes: +30dB
    if (wm8960_write_reg(WM8960_REG_LINVOL, 0x13F) != ESP_OK) return ESP_FAIL;
    if (wm8960_write_reg(WM8960_REG_RINVOL, 0x13F) != ESP_OK) return ESP_FAIL;

    // Additional Control 1: ADC HPF enabled, thermal shutdown enabled
    if (wm8960_write_reg(WM8960_REG_ADDCTL1, 0x0C0) != ESP_OK) return ESP_FAIL;

    ESP_LOGI(TAG, "WM8960 configured");
    return ESP_OK;
}

//=============================================================================
// I2S INITIALIZATION
//=============================================================================

static esp_err_t wm8960_init_i2s(void)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &tx_handle, &rx_handle));

    i2s_std_config_t std_cfg = {
        .clk_cfg = {
            .sample_rate_hz = SAMPLE_RATE_HZ,
            .clk_src = I2S_CLK_SRC_DEFAULT,
            .mclk_multiple = I2S_MCLK_MULTIPLE_256,
        },
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_MCLK_PIN,
            .bclk = I2S_BCLK_PIN,
            .ws = I2S_WS_PIN,
            .dout = I2S_DOUT_PIN,
            .din = I2S_DIN_PIN,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };

    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx_handle, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(rx_handle, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(tx_handle));
    ESP_ERROR_CHECK(i2s_channel_enable(rx_handle));

    ESP_LOGI(TAG, "I2S initialized (MCLK GPIO%d, %.3f MHz)",
             I2S_MCLK_PIN, (float)(SAMPLE_RATE_HZ * 256) / 1000000.0f);
    return ESP_OK;
}

//=============================================================================
// PUBLIC FUNCTIONS
//=============================================================================

esp_err_t audio_codec_init(void)
{
    if (initialized) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing WM8960...");

    esp_err_t ret = wm8960_init_i2c();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C init failed");
        return ret;
    }

    ret = wm8960_configure();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WM8960 configuration failed - check hardware");
        i2c_driver_delete(I2C_NUM_0);
        return ret;
    }

    ret = wm8960_init_i2s();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2S init failed");
        i2c_driver_delete(I2C_NUM_0);
        return ret;
    }

    initialized = true;
    ESP_LOGI(TAG, "WM8960 ready");
    return ESP_OK;
}

esp_err_t audio_codec_set_input(codec_input_t input)
{
    ESP_LOGI(TAG, "Input: %s", input == CODEC_INPUT_MIC ? "MIC" : "LINE");
    return ESP_OK;
}

esp_err_t audio_codec_set_output(codec_output_t output)
{
    ESP_LOGI(TAG, "Output: %s", output == CODEC_OUTPUT_SPEAKER ? "SPEAKER" : "LINE");
    return ESP_OK;
}

esp_err_t audio_codec_set_input_gain(uint8_t gain)
{
    if (gain > 31) gain = 31;
    uint16_t reg_val = 0x100 | (gain & 0x3F);
    wm8960_write_reg(WM8960_REG_LINVOL, reg_val);
    wm8960_write_reg(WM8960_REG_RINVOL, reg_val);
    return ESP_OK;
}

esp_err_t audio_codec_set_output_volume(uint8_t volume)
{
    if (volume > 31) volume = 31;
    // Map 0-31 across HP volume range (0x30=-73dB to 0x7F=+6dB)
    uint16_t vol_reg = 0x30 + ((uint16_t)volume * (0x7F - 0x30) / 31);
    uint16_t reg_val = 0x100 | vol_reg;
    wm8960_write_reg(WM8960_REG_LOUT1, reg_val);
    wm8960_write_reg(WM8960_REG_ROUT1, reg_val);
    return ESP_OK;
}

esp_err_t audio_codec_read(int16_t *buffer, size_t sample_count, size_t *samples_read)
{
    if (!initialized || !buffer) {
        if (samples_read) *samples_read = 0;
        return ESP_FAIL;
    }

    static int16_t stereo_buffer[SAMPLES_PER_FRAME * 2];

    size_t bytes_read = 0;
    size_t bytes_to_read = sample_count * 2 * sizeof(int16_t);

    esp_err_t ret = i2s_channel_read(rx_handle, stereo_buffer, bytes_to_read,
                                     &bytes_read, portMAX_DELAY);
    if (ret != ESP_OK) {
        if (samples_read) *samples_read = 0;
        return ret;
    }

    // Extract left channel (mono)
    size_t mono_samples = bytes_read / (2 * sizeof(int16_t));
    for (size_t i = 0; i < mono_samples; i++) {
        buffer[i] = stereo_buffer[i * 2];
    }

    if (samples_read) *samples_read = mono_samples;
    return ESP_OK;
}

esp_err_t audio_codec_write(const int16_t *buffer, size_t sample_count)
{
    if (!initialized || !buffer) {
        return ESP_FAIL;
    }

    static int16_t stereo_buffer[SAMPLES_PER_FRAME * 2];

    for (size_t i = 0; i < sample_count; i++) {
        stereo_buffer[i * 2] = buffer[i];
#if DEVICE_TYPE_BASE
        // Invert right channel for differential transformer output
        stereo_buffer[i * 2 + 1] = -buffer[i];
#else
        // Pack: same signal on both channels
        stereo_buffer[i * 2 + 1] = buffer[i];
#endif
    }

    size_t bytes_written = 0;
    size_t bytes_to_write = sample_count * 2 * sizeof(int16_t);

    esp_err_t ret = i2s_channel_write(tx_handle, stereo_buffer, bytes_to_write,
                                      &bytes_written, portMAX_DELAY);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2S write failed: %d", ret);
    }
    return ret;
}

bool audio_codec_is_initialized(void)
{
    return initialized;
}

esp_err_t audio_codec_set_sidetone(bool enable, float level)
{
    ESP_LOGI(TAG, "Sidetone: %s (%.2f)", enable ? "ON" : "OFF", level);
    return ESP_OK;
}

void audio_codec_deinit(void)
{
    if (!initialized) return;

    if (tx_handle) {
        i2s_channel_disable(tx_handle);
        i2s_del_channel(tx_handle);
        tx_handle = NULL;
    }
    if (rx_handle) {
        i2s_channel_disable(rx_handle);
        i2s_del_channel(rx_handle);
        rx_handle = NULL;
    }

    i2c_driver_delete(I2C_NUM_0);
    initialized = false;
    ESP_LOGI(TAG, "Codec deinitialized");
}
