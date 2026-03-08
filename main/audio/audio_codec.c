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
#define WM8960_REG_IFACE1          0x07
#define WM8960_REG_CLOCK1          0x04
#define WM8960_REG_LINVOL          0x00
#define WM8960_REG_RINVOL          0x01
#define WM8960_REG_LOUT1           0x02
#define WM8960_REG_ROUT1           0x03

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

    // Power up
    if (wm8960_write_reg(WM8960_REG_POWER1, 0x00FF) != ESP_OK) return ESP_FAIL;
    if (wm8960_write_reg(WM8960_REG_POWER2, 0x01FF) != ESP_OK) return ESP_FAIL;

    // Configure I2S interface
    if (wm8960_write_reg(WM8960_REG_IFACE1, 0x0002) != ESP_OK) return ESP_FAIL;

    // Set sample rate
    if (wm8960_write_reg(WM8960_REG_CLOCK1, 0x0000) != ESP_OK) return ESP_FAIL;

    // Set default volumes
    if (wm8960_write_reg(WM8960_REG_LINVOL, 0x0117) != ESP_OK) return ESP_FAIL;
    if (wm8960_write_reg(WM8960_REG_RINVOL, 0x0117) != ESP_OK) return ESP_FAIL;
    if (wm8960_write_reg(WM8960_REG_LOUT1, 0x0179) != ESP_OK) return ESP_FAIL;
    if (wm8960_write_reg(WM8960_REG_ROUT1, 0x0179) != ESP_OK) return ESP_FAIL;

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
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
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
    // Configure WM8960 input mux (implement based on datasheet)
    // TODO: Set appropriate registers for input selection
#endif

    return ESP_OK;
}

esp_err_t audio_codec_set_output(codec_output_t output)
{
    current_output = output;
    ESP_LOGI(TAG, "Output set to: %s",
             output == CODEC_OUTPUT_SPEAKER ? "SPEAKER" : "LINE");

#if !SIMULATE_HARDWARE
    // Configure WM8960 output mux (implement based on datasheet)
    // TODO: Set appropriate registers for output selection
#endif

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

esp_err_t audio_codec_read(int16_t *buffer, size_t sample_count)
{
    if (!initialized || !buffer) {
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

        return ESP_OK;
    }

#if !SIMULATE_HARDWARE
    // Read from WM8960 via I2S
    size_t bytes_read = 0;
    size_t bytes_to_read = sample_count * sizeof(int16_t);

    esp_err_t ret = i2s_channel_read(rx_handle, buffer, bytes_to_read,
                                     &bytes_read, portMAX_DELAY);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2S read failed: %d", ret);
        return ret;
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
    // Write to WM8960 via I2S
    size_t bytes_written = 0;
    size_t bytes_to_write = sample_count * sizeof(int16_t);

    esp_err_t ret = i2s_channel_write(tx_handle, buffer, bytes_to_write,
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