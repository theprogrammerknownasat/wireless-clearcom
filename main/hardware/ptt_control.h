/**
* @file ptt_control.h
 * @brief PTT State Machine (RS-701 Compatible)
 *
 * Implements PTT button behavior matching ClearCom RS-701:
 * - Quick press: Toggle latch on/off
 * - Hold >200ms: Momentary PTT
 */

#ifndef PTT_CONTROL_H
#define PTT_CONTROL_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

//=============================================================================
// PTT STATES
//=============================================================================

typedef enum {
    PTT_IDLE = 0,       // Mic off, not transmitting
    PTT_LATCHED,        // Mic on (toggled), transmitting
    PTT_MOMENTARY       // Mic on (held), transmitting
} ptt_state_t;

//=============================================================================
// PTT CALLBACK
//=============================================================================

/**
 * @brief PTT state change callback
 * @param state New PTT state
 * @param transmitting true if currently transmitting
 */
typedef void (*ptt_state_callback_t)(ptt_state_t state, bool transmitting);

//=============================================================================
// PUBLIC FUNCTIONS
//=============================================================================

/**
 * @brief Initialize PTT control
 * @param callback Callback for PTT state changes
 * @return ESP_OK on success
 */
esp_err_t ptt_control_init(ptt_state_callback_t callback);

/**
 * @brief Handle PTT button event (called by GPIO handler)
 * @param pressed true if button pressed, false if released
 * @param hold_time_ms How long button has been held
 */
void ptt_control_button_event(bool pressed, uint32_t hold_time_ms);

/**
 * @brief Get current PTT state
 * @return Current PTT state
 */
ptt_state_t ptt_control_get_state(void);

/**
 * @brief Check if currently transmitting
 * @return true if transmitting (latched or momentary)
 */
bool ptt_control_is_transmitting(void);

#endif // PTT_CONTROL_H