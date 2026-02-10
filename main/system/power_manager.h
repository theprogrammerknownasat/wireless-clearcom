/**
 * @file power_manager.h
 * @brief Power Management and Sleep Modes
 *
 * Handles power states for belt pack:
 * - Light sleep after 90 seconds of inactivity
 * - Deep sleep after 20 minutes of inactivity
 */

#ifndef POWER_MANAGER_H
#define POWER_MANAGER_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

//=============================================================================
// POWER STATES
//=============================================================================

typedef enum {
    POWER_STATE_ACTIVE = 0,     // Normal operation
    POWER_STATE_LIGHT_SLEEP,    // WiFi off, wake on button
    POWER_STATE_DEEP_SLEEP      // Everything off, wake on button
} power_state_t;

//=============================================================================
// POWER CALLBACKS
//=============================================================================

/**
 * @brief Power state change callback
 * @param state New power state
 */
typedef void (*power_state_callback_t)(power_state_t state);

//=============================================================================
// PUBLIC FUNCTIONS
//=============================================================================

/**
 * @brief Initialize power manager
 * @param callback Callback for power state changes
 * @return ESP_OK on success
 */
esp_err_t power_manager_init(power_state_callback_t callback);

/**
 * @brief Update activity timestamp (prevents sleep)
 *
 * Call this when:
 * - Button pressed
 * - Audio packet received
 * - Any user interaction
 */
void power_manager_activity(void);

/**
 * @brief Check if sleep timeout reached
 * @param light_sleep Set to true if light sleep timeout reached
 * @param deep_sleep Set to true if deep sleep timeout reached
 */
void power_manager_check_timeout(bool *light_sleep, bool *deep_sleep);

/**
 * @brief Enter light sleep mode
 * @return ESP_OK on success
 *
 * Wakes on:
 * - PTT button press
 * - Call button press
 */
esp_err_t power_manager_enter_light_sleep(void);

/**
 * @brief Enter deep sleep mode
 * @return ESP_OK on success (never returns - device resets on wake)
 *
 * Wakes on:
 * - PTT button press
 */
esp_err_t power_manager_enter_deep_sleep(void);

/**
 * @brief Get current power state
 * @return Current power state
 */
power_state_t power_manager_get_state(void);

/**
 * @brief Get time since last activity (ms)
 * @return Milliseconds since last activity
 */
uint32_t power_manager_get_idle_time(void);

#endif // POWER_MANAGER_H