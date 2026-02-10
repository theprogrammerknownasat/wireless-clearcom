/**
 * @file diagnostics.h
 * @brief System Diagnostics and Self-Test
 *
 * Runs comprehensive self-test on boot to verify all hardware.
 * CRITICAL: If SIMULATE_HARDWARE=0 and hardware fails, system HALTS.
 */

#ifndef DIAGNOSTICS_H
#define DIAGNOSTICS_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

//=============================================================================
// TEST RESULTS
//=============================================================================

typedef enum {
    TEST_NOT_RUN = 0,
    TEST_PASS,
    TEST_FAIL,
    TEST_SKIP
} test_result_t;

typedef struct {
    test_result_t codec_i2c;        // WM8960 I2C communication
    test_result_t codec_audio;      // Audio loopback test
    test_result_t opus_encode;      // Opus encoder
    test_result_t opus_decode;      // Opus decoder
    test_result_t wifi;             // WiFi initialization
    test_result_t udp;              // UDP socket creation
    test_result_t battery_adc;      // Battery ADC (pack only)
    test_result_t gpio_buttons;     // Button GPIOs (pack only)
    test_result_t gpio_leds;        // LED GPIOs
    test_result_t nvs;              // NVS storage
    bool all_passed;                // True if all critical tests passed
    uint8_t pass_count;             // Number of tests passed
    uint8_t fail_count;             // Number of tests failed
    uint8_t total_count;            // Total tests run
} diagnostics_result_t;

//=============================================================================
// PUBLIC FUNCTIONS
//=============================================================================

/**
 * @brief Run full system self-test
 * @param results Pointer to results structure
 * @return ESP_OK if all critical tests passed, ESP_FAIL otherwise
 *
 * CRITICAL BEHAVIOR:
 * - If SIMULATE_HARDWARE=1: Skips hardware tests, always returns ESP_OK
 * - If SIMULATE_HARDWARE=0: Requires all hardware, halts on failure
 */
esp_err_t diagnostics_run_self_test(diagnostics_result_t *results);

/**
 * @brief Print diagnostics results to log
 * @param results Pointer to results structure
 */
void diagnostics_print_results(const diagnostics_result_t *results);

/**
 * @brief Get free heap size
 * @return Free heap in bytes
 */
size_t diagnostics_get_free_heap(void);

/**
 * @brief Get minimum free heap (since boot)
 * @return Minimum free heap in bytes
 */
size_t diagnostics_get_min_free_heap(void);

/**
 * @brief Print system information
 */
void diagnostics_print_system_info(void);

#endif // DIAGNOSTICS_H