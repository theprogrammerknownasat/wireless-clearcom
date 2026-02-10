/**
 * @file config_common.h
 * @brief Common Configuration (Shared by Base and Pack)
 *
 * Settings that apply to both base station and belt pack.
 */

#ifndef CONFIG_COMMON_H
#define CONFIG_COMMON_H

#include <stdint.h>

//=============================================================================
// AUDIO CONFIGURATION
//=============================================================================

// Sample rate (Hz) - DO NOT CHANGE unless you know what you're doing
#define SAMPLE_RATE_HZ          16000

// Frame size for Opus encoding (ms)
// Options: 10, 20, 40, 60 (20ms recommended for balance of latency/efficiency)
#define FRAME_SIZE_MS           20

// Samples per frame (calculated)
#define SAMPLES_PER_FRAME       ((SAMPLE_RATE_HZ * FRAME_SIZE_MS) / 1000)

// Opus bitrate (bits per second)
// Range: 6000-510000, recommend 24000 for voice
#define OPUS_BITRATE            24000

// Opus complexity (0-10, higher = better quality but more CPU)
// Recommend: 5 for balance
#define OPUS_COMPLEXITY         5

// Enable audio limiter to prevent clipping
// 0 = disabled, 1 = enabled (recommended)
#define ENABLE_AUDIO_LIMITER    1

// Limiter threshold (0.0 to 1.0, where 1.0 = full scale)
#define LIMITER_THRESHOLD       0.95f

//=============================================================================
// NETWORK CONFIGURATION
//=============================================================================

// WiFi SSID (network name)
#define WIFI_SSID               "ClearCom_Base"

// WiFi Password (minimum 8 characters for WPA2)
#define WIFI_PASSWORD           "clearcom123"

// WiFi Channel (1, 6, or 11 recommended for 2.4GHz to avoid overlap)
#define WIFI_CHANNEL            6

// Hide SSID from casual discovery
// 0 = visible, 1 = hidden (recommended for security)
#define WIFI_HIDDEN_SSID        1

// UDP port for audio packets
#define UDP_PORT                5000

// Maximum packet size (bytes)
#define MAX_PACKET_SIZE         512

// Packet loss threshold for status warnings (percent)
#define PACKET_LOSS_WARN_THRESHOLD  2.0f

//=============================================================================
// LOGGING CONFIGURATION
//=============================================================================

// Log levels: ESP_LOG_NONE, ESP_LOG_ERROR, ESP_LOG_WARN, ESP_LOG_INFO, ESP_LOG_DEBUG
// Production: ESP_LOG_WARN
// Testing: ESP_LOG_INFO
// Debug: ESP_LOG_DEBUG
#define LOG_LEVEL               ESP_LOG_INFO

// Stats display interval (milliseconds)
#define STATS_INTERVAL_MS       5000

//=============================================================================
// GPIO PIN ASSIGNMENTS (ESP32-S3)
//=============================================================================
// NOTE: These will be finalized once hardware schematic is complete

// I2S (Audio Codec)
#define I2S_BCLK_PIN            GPIO_NUM_4
#define I2S_WS_PIN              GPIO_NUM_5
#define I2S_DOUT_PIN            GPIO_NUM_6   // ESP32 -> WM8960
#define I2S_DIN_PIN             GPIO_NUM_7   // WM8960 -> ESP32

// I2C (Audio Codec Control)
#define I2C_SDA_PIN             GPIO_NUM_8
#define I2C_SCL_PIN             GPIO_NUM_9

// LEDs (all devices)
#define LED_POWER_PIN           GPIO_NUM_10
#define LED_CALL_PIN            GPIO_NUM_11
#define LED_STATUS_PIN          GPIO_NUM_12

// Device-specific pins defined in config_base.h and config_pack.h

//=============================================================================
// TIMING CONSTANTS
//=============================================================================

// Network reconnect delay (milliseconds)
#define WIFI_RECONNECT_DELAY_MS 2000

// Watchdog timeout (seconds)
#define WATCHDOG_TIMEOUT_SEC    10

#endif // CONFIG_COMMON_H