/**
* @file test_mode_base.h
 * @brief Base Station Test Mode Header
 */

#ifndef TEST_MODE_BASE_H
#define TEST_MODE_BASE_H

#include "esp_err.h"
#include <stdbool.h>
#include "config.h"  // Need this for DEVICE_TYPE_BASE and TEST_MODE_ENABLE

#if DEVICE_TYPE_BASE && TEST_MODE_ENABLE

/**
 * @brief Start base station test mode
 *
 * Generates 440Hz test tone and outputs to party line.
 * Monitors CALL_RX pin for incoming call signals.
 *
 * @return ESP_OK on success
 */
esp_err_t test_mode_start(void);

/**
 * @brief Stop test mode
 */
void test_mode_stop(void);

/**
 * @brief Check if test mode is running
 * @return true if running, false otherwise
 */
bool test_mode_is_running(void);

#else

// Stub functions for when test mode is disabled
static inline esp_err_t test_mode_start(void) { return ESP_OK; }
static inline void test_mode_stop(void) { }
static inline bool test_mode_is_running(void) { return false; }

#endif // DEVICE_TYPE_BASE && TEST_MODE_ENABLE

#endif // TEST_MODE_BASE_H