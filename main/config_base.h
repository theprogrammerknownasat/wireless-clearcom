/**
 * @file config_base.h
 * @brief Base Station Specific Configuration
 *
 * Settings that only apply to base station devices.
 */

#ifndef CONFIG_BASE_H
#define CONFIG_BASE_H

//=============================================================================
// DEVICE IDENTIFICATION
//=============================================================================

// Base station device ID (0x80-0xFF range, high bit set)
// Each base station should have a unique ID
#define DEVICE_ID               0x80

// Which belt pack this base station is paired with
#define PAIRED_PACK_ID          0x01

//=============================================================================
// NETWORK CONFIGURATION (Base = WiFi AP)
//=============================================================================

// Static IP for base station
#define BASE_STATION_IP         "192.168.4.1"

// Maximum number of connected stations (should be 1 for 1:1 pairing)
#define MAX_STA_CONN            1

//=============================================================================
// CLEARCOM PARTY LINE INTERFACE
//=============================================================================

// Party line audio input gain (0-31)
// Adjust if audio from wired ClearCom is too quiet or too loud
#define PARTYLINE_INPUT_GAIN    20

// Party line audio output gain (0-31)
// Adjust if audio to wired ClearCom is too quiet or too loud
#define PARTYLINE_OUTPUT_GAIN   20

// Enable DC blocking on party line input (recommended)
// 0 = disabled, 1 = enabled
#define PARTYLINE_DC_BLOCKING   1

//=============================================================================
// GPIO PIN ASSIGNMENTS (Base Station Specific)
//=============================================================================

// I2S MCLK (base uses GPIO 2)
#define I2S_MCLK_PIN            GPIO_NUM_2

// PTT Mirror LED (shows connected pack's PTT state)
#define LED_PTT_MIRROR_PIN      GPIO_NUM_13

// Call detection from party line (ADC input)
// Reads scaled-down call voltage to detect incoming calls
#define CALL_RX_PIN             GPIO_NUM_1   // ADC1_CH0
#define CALL_RX_ADC_CHANNEL     ADC_CHANNEL_0

// Call voltage thresholds (adjust based on your voltage divider)
#define CALL_VOLTAGE_THRESHOLD  1.5f  // Volts - above this = call detected
#define CALL_DEBOUNCE_MS        50    // Milliseconds - debounce time

// Optional passthrough detect (if 3-pin XLR female is used)
// Set to -1 if not used
#define PASSTHROUGH_DETECT_PIN  -1

//=============================================================================
// LED CONFIGURATION (Base Station)
//=============================================================================

// Enable/disable individual LEDs (0=off, 1=on)
#define LED_POWER_ENABLE        1    // Power indicator
#define LED_STATUS_ENABLE       1    // Signal quality indicator
#define LED_CALL_ENABLE         1    // Call signal indicator
#define LED_PTT_MIRROR_ENABLE   1    // Mirror pack's PTT state
#define LED_RECEIVE_ENABLE      0    // Optional: RX indicator

// LED brightness (0-100 percent)
#define LED_BRIGHTNESS_PCT      80

// Status LED blink rates (milliseconds)
#define STATUS_LED_BLINK_SLOW   1000  // Good signal
#define STATUS_LED_BLINK_FAST   200   // Poor signal

//=============================================================================
// POWER MANAGEMENT (Base Station)
//=============================================================================

// Base station is powered by 30V ClearCom line - no sleep modes
// These are here for consistency but not used

#define ENABLE_LIGHT_SLEEP      0
#define ENABLE_DEEP_SLEEP       0

//=============================================================================
// DIAGNOSTICS
//=============================================================================

// Enable test mode (generates 440Hz tone, tests party line output)
// Set to 1 for testing, 0 for production
#define TEST_MODE_ENABLE        1

// Enable self-test on boot
#define SELFTEST_ENABLE         1

// Individual self-test modules (only if SELFTEST_ENABLE = 1)
#define SELFTEST_WM8960         1
#define SELFTEST_WIFI           1
#define SELFTEST_PARTYLINE      1
#define SELFTEST_LEDS           1
#define SELFTEST_OPUS           1

#endif // CONFIG_BASE_H