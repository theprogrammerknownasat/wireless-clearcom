/**
 * @file test_mode_pack.h
 * @brief Belt Pack Test Mode
 * 
 * Simple test mode that outputs 440Hz tone to headphones
 * to verify WM8960 audio output is working.
 */

#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Start test mode
 * Generates 440Hz tone and outputs to headphones
 */
esp_err_t test_mode_start(void);

/**
 * @brief Stop test mode
 */
void test_mode_stop(void);

#ifdef __cplusplus
}
#endif