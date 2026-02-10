/**
 * @file gpio_control.h
 * @brief GPIO Control for Buttons and LEDs
 *
 * Handles PTT button, call button, and all LED indicators.
 */

#ifndef GPIO_CONTROL_H
#define GPIO_CONTROL_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

//=============================================================================
// LED CONTROL
//=============================================================================

typedef enum {
    LED_POWER = 0,
    LED_STATUS,
    LED_CALL,
#if DEVICE_TYPE_PACK
    LED_PTT,
    LED_RECEIVE,
#else
    LED_PTT_MIRROR,
#endif
    LED_COUNT
} led_id_t;

typedef enum {
    LED_OFF = 0,
    LED_ON,
    LED_BLINK_SLOW,    // 1Hz
    LED_BLINK_FAST     // 5Hz
} led_state_t;

//=============================================================================
// BUTTON CALLBACKS
//=============================================================================

/**
 * @brief PTT button event callback
 * @param pressed true if button pressed, false if released
 * @param hold_time_ms How long button has been held (0 if just pressed)
 */
typedef void (*ptt_callback_t)(bool pressed, uint32_t hold_time_ms);

/**
 * @brief Call button event callback
 * @param pressed true if button pressed, false if released
 */
typedef void (*call_callback_t)(bool pressed);

//=============================================================================
// PUBLIC FUNCTIONS
//=============================================================================

/**
 * @brief Initialize GPIO control
 * @param ptt_cb Callback for PTT button events
 * @param call_cb Callback for call button events
 * @return ESP_OK on success
 */
esp_err_t gpio_control_init(ptt_callback_t ptt_cb, call_callback_t call_cb);

/**
 * @brief Set LED state
 * @param led LED identifier
 * @param state LED state (ON, OFF, BLINK_SLOW, BLINK_FAST)
 */
void gpio_control_set_led(led_id_t led, led_state_t state);

/**
 * @brief Set LED brightness (0-100%)
 * @param brightness Brightness percentage
 */
void gpio_control_set_brightness(uint8_t brightness);

/**
 * @brief Get PTT button current state
 * @return true if pressed, false otherwise
 */
bool gpio_control_is_ptt_pressed(void);

/**
 * @brief Get call button current state
 * @return true if pressed, false otherwise
 */
bool gpio_control_is_call_pressed(void);

/**
 * @brief Deinitialize GPIO control
 */
void gpio_control_deinit(void);

#endif // GPIO_CONTROL_H