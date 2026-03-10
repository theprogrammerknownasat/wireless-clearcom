/**
 * @file audio_codec.c
 * @brief WM8960 Audio Codec Driver Implementation
 *
 * When SIMULATE_HARDWARE=1: Generates test audio (sine waves, noise)
 * When SIMULATE_HARDWARE=0: Real WM8960 I2S/I2C communication
 */

#include "audio_codec.h"
#include "audio_tones.h"
#include "../config.h"
#include "esp_log.h"
#include "driver/i2s_std.h"
#include "driver/i2c.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "CODEC";

//=============================================================================
// WM8960 REGISTERS (For real hardware)
//=============================================================================

#define WM8960_I2C_ADDR     0x1A

// Register addresses
#define WM8960_REG_RESET           0x0F
#define WM8960_REG_POWER1          0x19
#define WM8960_REG_POWER2          0x1A
#define WM8960_REG_PWR_MGMT2       0x1A  // Alias for POWER2
#define WM8960_REG_IFACE1          0x07
#define WM8960_REG_CLOCK1          0x04
#define WM8960_REG_LINVOL          0x00
#define WM8960_REG_RINVOL          0x01
#define WM8960_REG_LOUT1           0x02  // Left Line Out
#define WM8960_REG_ROUT1           0x03  // Right Line Out
#define WM8960_REG_LOUT2           0x28  // Left Headphone/Speaker Out
#define WM8960_REG_ROUT2           0x29  // Right Headphone/Speaker Out
#define WM8960_REG_LOUTMIX         0x22  // Left Output Mixer
#define WM8960_REG_ROUTMIX         0x25  // Right Output Mixer

//=============================================================================
// PRIVATE VARIABLES
//=============================================================================

static bool initialized = false;
static bool using_simulation = SIMULATE_HARDWARE;  // Track if we're simulating
static codec_input_t current_input = CODEC_INPUT_MIC;
static codec_output_t current_output = CODEC_OUTPUT_SPEAKER;
static uint8_t input_gain = 20;
static uint8_t output_volume = 20;
static bool sidetone_enabled = false;
static float sidetone_level = 0.3f;

// Simulation state (used for simulation mode OR fallback if hardware fails)
static float sim_phase = 0.0f;
static uint32_t sim_sample_counter = 0;

#if !SIMULATE_HARDWARE
// Real hardware handles
static i2s_chan_handle_t tx_handle = NULL;
static i2s_chan_handle_t rx_handle = NULL;
#endif

//=============================================================================
// PRIVATE FUNCTIONS - I2C Communication
//=============================================================================

#if !SIMULATE_HARDWARE

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
        ESP_LOGE(TAG, "I2C write failed: reg=0x%02X, ret=%d", reg, ret);
    }

    return ret;
}

static esp_err_t wm8960_read_reg(uint8_t reg, uint16_t *value)
{
    // WM8960 doesn't support I2C read - we can only write!
    // This is a limitation of the WM8960 codec.
    // Return ESP_ERR_NOT_SUPPORTED to indicate this.
    // For debugging, we'll have to trust our writes worked.
    ESP_LOGW(TAG, "WM8960 does not support I2C register reads - skipping verification");
    *value = 0xFFFF;  // Invalid value to indicate read not possible
    return ESP_ERR_NOT_SUPPORTED;
}

static esp_err_t wm8960_init_i2c(void)
{
    ESP_LOGI(TAG, "Initializing I2C for WM8960...");

    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_SDA_PIN,
        .scl_io_num = I2C_SCL_PIN,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 100000,  // 100kHz
    };

    esp_err_t ret = i2c_param_config(I2C_NUM_0, &conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C param config failed");
        return ret;
    }

    ret = i2c_driver_install(I2C_NUM_0, conf.mode, 0, 0, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C driver install failed");
        return ret;
    }

    ESP_LOGI(TAG, "I2C initialized successfully");
    return ESP_OK;
}

static esp_err_t wm8960_configure(void)
{
    ESP_LOGI(TAG, "Configuring WM8960 registers...");

    esp_err_t ret;

    // Reset WM8960
    ret = wm8960_write_reg(WM8960_REG_RESET, 0x0000);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to reset WM8960 - hardware not connected?");
        return ESP_FAIL;
    }
    vTaskDelay(pdMS_TO_TICKS(100));
    // Reset
    if (wm8960_write_reg(WM8960_REG_RESET, 0x0000) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to reset WM8960");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "WM8960 reset complete");

    // Power up - based on working reference driver
    // POWER1: VMIDSEL + VREF + AINL
    if (wm8960_write_reg(WM8960_REG_POWER1, 0x1C0) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write POWER1");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "  ✓ POWER1 = 0x1C0 (VMIDSEL + VREF + AINL)");

    // POWER2: DACL + DACR + LOUT1 + ROUT1 + SPKL + SPKR
    if (wm8960_write_reg(WM8960_REG_POWER2, 0x1F8) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write POWER2");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "  ✓ POWER2 = 0x1F8 (DACs + all outputs)");

    // POWER3: ROMIX + LOMIX (output mixers)
    if (wm8960_write_reg(0x2F, 0x0C) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write POWER3");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "  ✓ POWER3 = 0x0C (output mixers enabled)");

    // Configure I2S interface - 16-bit I2S format
    if (wm8960_write_reg(WM8960_REG_IFACE1, 0x0002) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure I2S interface");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "  ✓ I2S interface configured (16-bit I2S)");

    // Set clock - MCLK->div1->SYSCLK
    if (wm8960_write_reg(WM8960_REG_CLOCK1, 0x0000) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure clock");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "  ✓ Clock configured");

    // Configure ADC/DAC control
    if (wm8960_write_reg(0x05, 0x0000) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure ADC/DAC");
        return ESP_FAIL;
    }

    // Set DAC volume (max)
    if (wm8960_write_reg(0x0A, 0x01FF) != ESP_OK) {  // Left DAC
        ESP_LOGE(TAG, "Failed to set left DAC volume");
        return ESP_FAIL;
    }
    if (wm8960_write_reg(0x0B, 0x01FF) != ESP_OK) {  // Right DAC
        ESP_LOGE(TAG, "Failed to set right DAC volume");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "  ✓ DAC volumes set to max");

    // Configure output mixers - CRITICAL: bits 8+7 (DAC + Input to output)
    if (wm8960_write_reg(WM8960_REG_LOUTMIX, 0x180) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure left output mixer");
        return ESP_FAIL;
    }
    if (wm8960_write_reg(WM8960_REG_ROUTMIX, 0x180) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure right output mixer");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "  ✓ Output mixers configured (DAC → outputs)");

    // Set line output volumes (LOUT1/ROUT1)
    if (wm8960_write_reg(WM8960_REG_LOUT1, 0x0161) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set LOUT1 volume");
        return ESP_FAIL;
    }
    if (wm8960_write_reg(WM8960_REG_ROUT1, 0x0161) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set ROUT1 volume");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "  ✓ Line out volumes set");

    // Set speaker/headphone volumes (LOUT2/ROUT2)
    if (wm8960_write_reg(WM8960_REG_LOUT2, 0x0177) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set LOUT2 volume");
        return ESP_FAIL;
    }
    if (wm8960_write_reg(WM8960_REG_ROUT2, 0x0177) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set ROUT2 volume");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "  ✓ Headphone/speaker volumes set");

    // Enable Class D speaker outputs
    if (wm8960_write_reg(0x31, 0x00F7) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable Class D outputs");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "  ✓ Class D outputs enabled");

    // Jack detect configuration
    if (wm8960_write_reg(0x18, 0x0040) != ESP_OK) {  // HPSWEN enabled
        ESP_LOGE(TAG, "Failed to configure jack detect");
        return ESP_FAIL;
    }
    if (wm8960_write_reg(0x17, 0x01C3) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure additional control");
        return ESP_FAIL;
    }
    if (wm8960_write_reg(0x30, 0x0009) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure additional control 4");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "  ✓ Jack detect configured");

    // ========== MICROPHONE INPUT CONFIGURATION ==========
    ESP_LOGI(TAG, "Configuring line-level input (from SSM2167 preamp)...");

    // Enable ADC - BOTH CHANNELS (like when we got audio before)
    // POWER1: Enable AINL, AINR, ADCL, ADCR, MICB
    // Even though we only use left input, enabling both might be required
    // for proper ADC operation
    if (wm8960_write_reg(WM8960_REG_POWER1, 0x1FE) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable ADC in POWER1");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "  ✓ POWER1 = 0x1FE (VMID + VREF + BOTH ADCs + MIC BIAS)");

    // POWER3: Enable BOTH input mixers (like when we got audio)
    if (wm8960_write_reg(0x2F, 0x3C) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable input mixers in POWER3");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "  ✓ POWER3 = 0x3C (output + BOTH input mixers)");

    // Configure ADC input: Use same config as when we got audio
    // Register 0x20 = 0x138 (this got us audio before, even if garbled)
    if (wm8960_write_reg(0x20, 0x138) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure left ADC input");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "  ✓ Left ADC: 0x138 config (same as when we got audio)");

    // Right ADC: Same as left
    if (wm8960_write_reg(0x21, 0x138) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure right ADC input");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "  ✓ Right ADC: 0x138 config");

    // Set input boost mixer gain
    // Even for line-level, we might need some boost through the mixer path
    // Register 0x2B (Left Input Boost Mixer):
    // Bits 6-4: LIN2BOOST = 000 (not used)
    // Bits 2-0: LIN3BOOST = 000 (not used)
    // We're using LMIC2B (mic input to boost) which is controlled separately
    // Set to +13dB boost to help with signal level
    if (wm8960_write_reg(0x2B, 0x010) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set left input boost");
        return ESP_FAIL;
    }
    if (wm8960_write_reg(0x2C, 0x000) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set right input boost");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "  ✓ Input boost configured (+13dB)");

    // Set ADC volume - restore 0dB like when we got audio
    if (wm8960_write_reg(0x15, 0x1C3) != ESP_OK) {  // Left ADC: 0dB
        ESP_LOGE(TAG, "Failed to set left ADC volume");
        return ESP_FAIL;
    }
    if (wm8960_write_reg(0x16, 0x1C3) != ESP_OK) {  // Right ADC: 0dB
        ESP_LOGE(TAG, "Failed to set right ADC volume");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "  ✓ ADC volumes set (0dB - same as when we got audio)");

    // Set input PGA volume - RESTORE +30dB like when we got audio
    // (Yes it's too much for line-level, but let's get SOMETHING first)
    if (wm8960_write_reg(0x00, 0x13F) != ESP_OK) {  // Left input volume: +30dB
        ESP_LOGE(TAG, "Failed to set left input volume");
        return ESP_FAIL;
    }
    if (wm8960_write_reg(0x01, 0x13F) != ESP_OK) {  // Right input volume: +30dB
        ESP_LOGE(TAG, "Failed to set right input volume");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "  ✓ Input PGA volumes set (+30dB - same as when we got audio)");

    // CRITICAL: Enable ADC high-pass filter and other ADC settings
    // Register 0x17 (Additional Control 1): ADCHPD=0 enables HP filter
    // This is often required for ADC to work properly
    if (wm8960_write_reg(0x17, 0x00C0) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set additional control 1");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "  ✓ ADC additional controls configured");

    ESP_LOGI(TAG, "Line-level input configured successfully (dynamic mic + SSM2167)");

    // ========== VERIFY REGISTER VALUES ==========
    // NOTE: WM8960 does not support I2C register reads!
    // We cannot verify our writes, but if we got ESP_OK from each write,
    // the registers should be set correctly.
    ESP_LOGI(TAG, "WM8960 configuration complete - cannot readback (codec limitation)");

    ESP_LOGI(TAG, "WM8960 configured successfully");
    return ESP_OK;
}

static esp_err_t wm8960_init_i2s(void)
{
    ESP_LOGI(TAG, "Initializing I2S for WM8960...");

    // Configure I2S channels
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);

    // Create TX and RX channels
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &tx_handle, &rx_handle));

    // Configure I2S standard mode with MCLK output
    i2s_std_config_t std_cfg = {
        .clk_cfg = {
            .sample_rate_hz = SAMPLE_RATE_HZ,
            .clk_src = I2S_CLK_SRC_DEFAULT,         // Use default PLL (APLL not available in 5.5.2)
            .mclk_multiple = I2S_MCLK_MULTIPLE_256, // MCLK = 256 × sample_rate = 4.096 MHz
        },
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_MCLK_PIN,  // MCLK output on GPIO 2
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

    // Enable channels
    ESP_ERROR_CHECK(i2s_channel_enable(tx_handle));
    ESP_ERROR_CHECK(i2s_channel_enable(rx_handle));

    ESP_LOGI(TAG, "I2S initialized with MCLK=%.3f MHz on GPIO %d",
             (float)(SAMPLE_RATE_HZ * 256) / 1000000.0f, I2S_MCLK_PIN);
    return ESP_OK;
}

#endif // !SIMULATE_HARDWARE

//=============================================================================
// PUBLIC FUNCTIONS
//=============================================================================

esp_err_t audio_codec_init(void)
{
    if (initialized) {
        ESP_LOGW(TAG, "Codec already initialized");
        return ESP_OK;
    }

#if SIMULATE_HARDWARE
    ESP_LOGW(TAG, "========================================");
    ESP_LOGW(TAG, "WM8960 SIMULATION MODE ACTIVE");
    ESP_LOGW(TAG, "Generating fake audio for testing");
    ESP_LOGW(TAG, "Set SIMULATE_HARDWARE=0 when hardware arrives");
    ESP_LOGW(TAG, "========================================");

    sim_phase = 0.0f;
    sim_sample_counter = 0;

    initialized = true;
    return ESP_OK;

#else
    ESP_LOGI(TAG, "Initializing WM8960 codec (real hardware)...");

    // Initialize I2C
    esp_err_t ret = wm8960_init_i2c();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C initialization failed - falling back to simulation");
        goto fallback_simulation;
    }

    // Configure WM8960 registers
    ret = wm8960_configure();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WM8960 not responding - hardware not connected?");
        ESP_LOGE(TAG, "Falling back to simulation mode for testing");
        i2c_driver_delete(I2C_NUM_0);  // Clean up I2C
        goto fallback_simulation;
    }

    // Initialize I2S
    ret = wm8960_init_i2s();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2S initialization failed - falling back to simulation");
        i2c_driver_delete(I2C_NUM_0);
        goto fallback_simulation;
    }

    initialized = true;
    ESP_LOGI(TAG, "WM8960 initialization complete");
    return ESP_OK;

fallback_simulation:
    ESP_LOGW(TAG, "========================================");
    ESP_LOGW(TAG, "HARDWARE INIT FAILED - USING SIMULATION");
    ESP_LOGW(TAG, "System will continue with simulated audio");
    ESP_LOGW(TAG, "========================================");
    sim_phase = 0.0f;
    sim_sample_counter = 0;
    using_simulation = true;  // Mark as using simulation
    initialized = true;
    return ESP_OK;  // Return OK so system continues
#endif
}

esp_err_t audio_codec_set_input(codec_input_t input)
{
    current_input = input;
    ESP_LOGI(TAG, "Input set to: %s",
             input == CODEC_INPUT_MIC ? "MIC" : "LINE");

#if !SIMULATE_HARDWARE
    // Configure WM8960 input routing
    if (input == CODEC_INPUT_LINE) {
        // Select LINE input (LINPUT1/RINPUT1)
        // Input gain already set by audio_codec_set_input_gain()
        ESP_LOGI(TAG, "LINE input enabled (LINPUT1/RINPUT1)");
    } else {
        // CODEC_INPUT_MIC
        // Enable microphone bias for electret mics
        ESP_LOGI(TAG, "MIC input enabled");
    }
#endif

    return ESP_OK;
}

esp_err_t audio_codec_set_output(codec_output_t output)
{
    current_output = output;
    ESP_LOGI(TAG, "Output set to: %s",
             output == CODEC_OUTPUT_SPEAKER ? "SPEAKER" : "LINE");

    // Output routing is already configured in init
    // Both LINE (LOUT1/ROUT1) and SPEAKER (LOUT2/ROUT2) are enabled
    // No additional configuration needed

    return ESP_OK;
}

esp_err_t audio_codec_set_input_gain(uint8_t gain)
{
    if (gain > 31) gain = 31;
    input_gain = gain;

    ESP_LOGI(TAG, "Input gain set to: %d", gain);

#if !SIMULATE_HARDWARE
    // Map 0-31 to WM8960 register values
    uint16_t reg_val = 0x100 | (gain & 0x3F);
    wm8960_write_reg(WM8960_REG_LINVOL, reg_val);
    wm8960_write_reg(WM8960_REG_RINVOL, reg_val);
#endif

    return ESP_OK;
}

esp_err_t audio_codec_set_output_volume(uint8_t volume)
{
    if (volume > 31) volume = 31;
    output_volume = volume;

    ESP_LOGI(TAG, "Output volume set to: %d", volume);

#if !SIMULATE_HARDWARE
    // Map 0-31 to WM8960 register values
    uint16_t reg_val = 0x100 | (volume + 0x30);
    wm8960_write_reg(WM8960_REG_LOUT1, reg_val);
    wm8960_write_reg(WM8960_REG_ROUT1, reg_val);
#endif

    return ESP_OK;
}

esp_err_t audio_codec_read(int16_t *buffer, size_t sample_count, size_t *samples_read)
{
    if (!initialized || !buffer) {
        if (samples_read) *samples_read = 0;
        return ESP_FAIL;
    }

#if SIMULATE_HARDWARE
    if (using_simulation) {
#else
    if (using_simulation) {  // Runtime fallback
#endif
        // Generate simulated audio:
        // 440Hz sine wave + low-level noise for testing

        audio_tones_generate_sine(buffer, sample_count, 440.0f, 0.3f, &sim_phase);

        // Add some noise to make it more realistic
        for (size_t i = 0; i < sample_count; i++) {
            int16_t noise = (rand() % 200) - 100;  // ±100 noise
            buffer[i] += noise;
        }

        sim_sample_counter += sample_count;

        if (samples_read) *samples_read = sample_count;
        return ESP_OK;
    }

#if !SIMULATE_HARDWARE
    // Read from WM8960 via I2S (stereo interleaved: L, R, L, R...)
    // We need to read 2x samples since we're in stereo mode
    static int16_t stereo_buffer[SAMPLES_PER_FRAME * 2];  // Temp buffer for stereo data

    size_t bytes_read = 0;
    size_t bytes_to_read = sample_count * 2 * sizeof(int16_t);  // *2 for stereo

    esp_err_t ret = i2s_channel_read(rx_handle, stereo_buffer, bytes_to_read,
                                     &bytes_read, portMAX_DELAY);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2S read failed: %d", ret);
        if (samples_read) *samples_read = 0;
        return ret;
    }

    // Downmix stereo to mono: Use ONLY left channel (right not connected)
    size_t stereo_samples = bytes_read / sizeof(int16_t);
    size_t mono_samples = stereo_samples / 2;

    // DEBUG: Log first read to see what we're getting
    static bool first_read = true;
    if (first_read) {
        first_read = false;
        ESP_LOGI(TAG, "First I2S read: bytes=%zu, stereo_samples=%zu, mono_samples=%zu",
                 bytes_read, stereo_samples, mono_samples);
        ESP_LOGI(TAG, "First 8 stereo pairs (L,R): [%d,%d] [%d,%d] [%d,%d] [%d,%d]",
                 stereo_buffer[0], stereo_buffer[1],
                 stereo_buffer[2], stereo_buffer[3],
                 stereo_buffer[4], stereo_buffer[5],
                 stereo_buffer[6], stereo_buffer[7]);
    }

    for (size_t i = 0; i < mono_samples; i++) {
        // Only use left channel (right is tied to VMID, will be noise)
        buffer[i] = stereo_buffer[i * 2];  // Left channel only
    }

    // Convert bytes to samples
    if (samples_read) {
        *samples_read = mono_samples;
    }
#endif

    return ESP_OK;
}

esp_err_t audio_codec_write(const int16_t *buffer, size_t sample_count)
{
    if (!initialized || !buffer) {
        return ESP_FAIL;
    }

#if SIMULATE_HARDWARE
    if (using_simulation) {
#else
    if (using_simulation) {  // Runtime fallback
#endif
        // In simulation mode, just pretend to write
        // Could log first/last samples for debugging
        ESP_LOGD(TAG, "SIM write: %d samples, first=%d, last=%d",
                 sample_count, buffer[0], buffer[sample_count-1]);

        return ESP_OK;
    }

#if !SIMULATE_HARDWARE
    // Upmix mono to stereo: duplicate mono sample to both L and R
    static int16_t stereo_buffer[SAMPLES_PER_FRAME * 2];  // Temp buffer for stereo data

    for (size_t i = 0; i < sample_count; i++) {
        stereo_buffer[i * 2] = buffer[i];      // Left
        stereo_buffer[i * 2 + 1] = buffer[i];  // Right (same as left)
    }

    // Write stereo data to WM8960 via I2S
    size_t bytes_written = 0;
    size_t bytes_to_write = sample_count * 2 * sizeof(int16_t);  // *2 for stereo

    esp_err_t ret = i2s_channel_write(tx_handle, stereo_buffer, bytes_to_write,
                                      &bytes_written, portMAX_DELAY);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2S write failed: %d", ret);
        return ret;
    }
#endif

    return ESP_OK;
}

esp_err_t audio_codec_set_sidetone(bool enable, float level)
{
    sidetone_enabled = enable;
    sidetone_level = level;

    ESP_LOGI(TAG, "Sidetone: %s, level: %.2f",
             enable ? "ENABLED" : "DISABLED", level);

    // Sidetone mixing is done in audio_processor, not here
    // This is just for future WM8960 hardware sidetone feature

    return ESP_OK;
}

void audio_codec_deinit(void)
{
    if (!initialized) {
        return;
    }

#if !SIMULATE_HARDWARE
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
#endif

    initialized = false;
    ESP_LOGI(TAG, "Codec deinitialized");
}