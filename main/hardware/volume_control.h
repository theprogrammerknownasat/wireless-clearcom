/**
 * @file volume_control.h
 * @brief Volume Potentiometer Control (Belt Pack Only)
 *
 * Reads a 10k potentiometer on ADC1_CH1 (GPIO2) and adjusts
 * the WM8960 headphone/speaker output volume accordingly.
 */

#ifndef VOLUME_CONTROL_H
#define VOLUME_CONTROL_H

#include <stdint.h>
#include "esp_err.h"

/**
 * @brief Initialize volume control
 *
 * Must be called after battery_init() so the ADC1 unit handle
 * can be shared. Configures ADC1_CH1 for reading the volume pot.
 *
 * @return ESP_OK on success
 */
esp_err_t volume_control_init(void);

/**
 * @brief Start the volume control task
 *
 * Spawns a FreeRTOS task that reads the pot every 100ms and
 * updates the codec output volume when a change is detected.
 *
 * @return ESP_OK on success
 */
esp_err_t volume_control_start(void);

/**
 * @brief Stop the volume control task
 * @return ESP_OK on success
 */
esp_err_t volume_control_stop(void);

/**
 * @brief Get current volume level (0-31)
 * @return Current volume level
 */
uint8_t volume_control_get_level(void);

/**
 * @brief Deinitialize volume control
 */
void volume_control_deinit(void);

#endif // VOLUME_CONTROL_H
