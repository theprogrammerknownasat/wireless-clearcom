/**
 * @file config_pack.h
 * @brief Belt Pack Specific Configuration
 *
 * Settings that only apply to belt pack devices.
 */

#ifndef CONFIG_PACK_H
#define CONFIG_PACK_H

//=============================================================================
// DEVICE IDENTIFICATION
//=============================================================================

// Belt pack device ID (0x01-0x7F range)
// Each belt pack should have a unique ID
#define DEVICE_ID               0x01

// Which base station this belt pack is paired with
#define PAIRED_BASE_ID          0x80

//=============================================================================
// NETWORK CONFIGURATION (Pack = WiFi Station)
//=============================================================================

// Base station IP to connect to
#define BASE_STATION_IP         "192.168.4.1"

//=============================================================================
// AUDIO CONFIGURATION (Belt Pack Specific)
//=============================================================================

// Microphone input gain (0-31)
// Adjust if mic is too quiet or too loud
// Typical: 15-25 for dynamic mics
#define MIC_GAIN_LEVEL          20

// Enable sidetone (hear yourself when talking)
// 0 = disabled, 1 = enabled (recommended for RS-701 emulation)
#define SIDETONE_ENABLE         1

// Sidetone level (0.0 to 1.0)
// How much of your mic signal you hear in your headset
// 0.0 = none, 0.3 = typical, 1.0 = full volume
#define SIDETONE_LEVEL          0.3f

//=============================================================================
// PTT BUTTON CONFIGURATION
//=============================================================================

// Time to hold button before switching from latch to momentary (ms)
// Typical: 200ms
#define PTT_HOLD_THRESHOLD_MS   200

//=============================================================================
// GPIO PIN ASSIGNMENTS (Belt Pack Specific)
//=============================================================================

// Buttons
#define BUTTON_PTT_PIN          GPIO_NUM_13
#define BUTTON_CALL_PIN         GPIO_NUM_14

// LEDs (in addition to common LEDs in config_common.h)
#define LED_PTT_PIN             GPIO_NUM_15
#define LED_RECEIVE_PIN         GPIO_NUM_16   // Optional

// Battery monitoring (ADC input)
#define BATTERY_ADC_CHANNEL     ADC_CHANNEL_0
#define BATTERY_ADC_PIN         GPIO_NUM_1

//=============================================================================
// LED CONFIGURATION (Belt Pack)
//=============================================================================

// Enable/disable individual LEDs (0=off, 1=on)
#define LED_POWER_ENABLE        1    // Power indicator
#define LED_PTT_ENABLE          1    // PTT state indicator
#define LED_CALL_ENABLE         1    // Call signal indicator
#define LED_STATUS_ENABLE       1    // Signal quality indicator
#define LED_RECEIVE_ENABLE      0    // Optional: RX indicator

// LED brightness (0-100 percent)
#define LED_BRIGHTNESS_PCT      80

// Status LED blink rates (milliseconds)
#define STATUS_LED_BLINK_SLOW   1000  // Good signal
#define STATUS_LED_BLINK_FAST   200   // Poor signal

//=============================================================================
// AUDIO TONE CONFIGURATION
//=============================================================================

// Tone structure definition
typedef struct {
    uint16_t frequency_hz;          // Tone frequency
    uint16_t duration_ms;           // Duration of single beep
    uint8_t  repeat_count;          // How many times to beep
    uint16_t repeat_interval_ms;    // Gap between repeats
} tone_config_t;

// Enable/disable tones (0=off, 1=on)
#define TONE_CONNECTED_ENABLE       1
#define TONE_DISCONNECTED_ENABLE    1
#define TONE_BATTERY_LOW_ENABLE     1
#define TONE_BATTERY_CRITICAL_ENABLE 1
#define TONE_CALL_ENABLE            1

// Tone definitions
// Connected to base station
static const tone_config_t TONE_CONNECTED = {
    .frequency_hz = 800,
    .duration_ms = 100,
    .repeat_count = 2,
    .repeat_interval_ms = 100
};

// Disconnected from base station
static const tone_config_t TONE_DISCONNECTED = {
    .frequency_hz = 400,
    .duration_ms = 500,
    .repeat_count = 1,
    .repeat_interval_ms = 0
};

// Battery low warning (10% remaining)
static const tone_config_t TONE_BATTERY_LOW = {
    .frequency_hz = 600,
    .duration_ms = 100,
    .repeat_count = 1,
    .repeat_interval_ms = 0
};

// Battery critical (3% remaining, repeats every 60 seconds)
static const tone_config_t TONE_BATTERY_CRITICAL = {
    .frequency_hz = 600,
    .duration_ms = 100,
    .repeat_count = 3,
    .repeat_interval_ms = 150
};

// Call button pressed / call received
static const tone_config_t TONE_CALL = {
    .frequency_hz = 1000,
    .duration_ms = 200,
    .repeat_count = 1,
    .repeat_interval_ms = 0
};

//=============================================================================
// POWER MANAGEMENT (Belt Pack)
//=============================================================================

// Battery voltage thresholds (volts)
#define BATTERY_FULL_VOLTAGE        4.2f   // 100% charge
#define BATTERY_LOW_VOLTAGE         3.3f   // 10% remaining
#define BATTERY_CRITICAL_VOLTAGE    3.0f   // 3% remaining
#define BATTERY_EMPTY_VOLTAGE       2.8f   // 0% (shutdown)

// Battery monitoring interval (seconds)
#define BATTERY_CHECK_INTERVAL_SEC  30

// Battery critical warning interval (seconds)
// How often to repeat warning tone when battery is critical
#define BATTERY_CRITICAL_WARN_INTERVAL 60

// Enable light sleep mode
// 0 = disabled, 1 = enabled
#define ENABLE_LIGHT_SLEEP          1

// Light sleep timeout (seconds)
// Enter light sleep after this long without receiving packets
#define LIGHT_SLEEP_TIMEOUT_SEC     90

// Enable deep sleep mode
// 0 = disabled, 1 = enabled
#define ENABLE_DEEP_SLEEP           1

// Deep sleep timeout (minutes)
// Enter deep sleep after this long without receiving packets
#define DEEP_SLEEP_TIMEOUT_MIN      20

//=============================================================================
// DIAGNOSTICS
//=============================================================================

// Enable self-test on boot
#define SELFTEST_ENABLE             1

// Individual self-test modules (only if SELFTEST_ENABLE = 1)
#define SELFTEST_WM8960             1
#define SELFTEST_WIFI               1
#define SELFTEST_BATTERY            1
#define SELFTEST_LEDS               1
#define SELFTEST_SPEAKER            1
#define SELFTEST_OPUS               1

#endif // CONFIG_PACK_H