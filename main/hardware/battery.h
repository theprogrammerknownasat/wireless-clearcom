/**
* @file battery.h
 * @brief Battery Monitoring (Belt Pack Only)
 *
 * Monitors battery voltage and estimates percentage remaining.
 */

#ifndef BATTERY_H
#define BATTERY_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

//=============================================================================
// BATTERY CALLBACKS
//=============================================================================

/**
 * @brief Battery status callback
 * @param voltage Current battery voltage
 * @param percent Estimated percentage (0-100)
 * @param is_low true if battery is low (10%)
 * @param is_critical true if battery is critical (3%)
 */
typedef void (*battery_callback_t)(float voltage, uint8_t percent,
                                    bool is_low, bool is_critical);

//=============================================================================
// PUBLIC FUNCTIONS
//=============================================================================

/**
 * @brief Initialize battery monitoring
 * @param callback Callback for battery status updates
 * @return ESP_OK on success
 */
esp_err_t battery_init(battery_callback_t callback);

/**
 * @brief Start battery monitoring task
 * @return ESP_OK on success
 */
esp_err_t battery_start(void);

/**
 * @brief Stop battery monitoring task
 * @return ESP_OK on success
 */
esp_err_t battery_stop(void);

/**
 * @brief Get current battery voltage
 * @return Battery voltage in volts
 */
float battery_get_voltage(void);

/**
 * @brief Get current battery percentage
 * @return Battery percentage (0-100)
 */
uint8_t battery_get_percent(void);

/**
 * @brief Check if battery is low
 * @return true if battery <10%
 */
bool battery_is_low(void);

/**
 * @brief Check if battery is critical
 * @return true if battery <3%
 */
bool battery_is_critical(void);

/**
 * @brief Deinitialize battery monitoring
 */
void battery_deinit(void);

#endif // BATTERY_H