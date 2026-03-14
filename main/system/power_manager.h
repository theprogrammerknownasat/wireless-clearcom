/**
 * @file power_manager.h
 * @brief Power Management and Sleep Modes (Belt Pack)
 *
 * Light sleep: CPU halts, WiFi stays connected, wakes on button or packet.
 *              Invisible to user except possible minor latency.
 * Deep sleep:  Full shutdown, device resets on wake (PTT or CALL button).
 */

#ifndef POWER_MANAGER_H
#define POWER_MANAGER_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

typedef enum {
    POWER_STATE_ACTIVE = 0,
    POWER_STATE_LIGHT_SLEEP,
    POWER_STATE_DEEP_SLEEP
} power_state_t;

typedef void (*power_state_callback_t)(power_state_t state);

esp_err_t power_manager_init(power_state_callback_t callback);
void power_manager_activity(void);
void power_manager_check_timeout(bool *light_sleep, bool *deep_sleep);
esp_err_t power_manager_enter_light_sleep(void);
esp_err_t power_manager_enter_deep_sleep(void);
power_state_t power_manager_get_state(void);
uint32_t power_manager_get_idle_time(void);

#endif // POWER_MANAGER_H
