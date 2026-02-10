/**
 * @file call_module.h
 * @brief Call Signaling Module
 *
 * Handles call button press/hold and call light indication.
 */

#ifndef CALL_MODULE_H
#define CALL_MODULE_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

//=============================================================================
// CALL STATES
//=============================================================================

typedef enum {
    CALL_IDLE = 0,          // No call active
    CALL_OUTGOING,          // Calling remote station
    CALL_INCOMING,          // Being called by remote station
    CALL_ACKNOWLEDGED       // Call acknowledged (both sides)
} call_state_t;

//=============================================================================
// CALL CALLBACKS
//=============================================================================

/**
 * @brief Call state change callback
 * @param state New call state
 * @param is_calling true if this device initiated the call
 */
typedef void (*call_state_callback_t)(call_state_t state, bool is_calling);

//=============================================================================
// PUBLIC FUNCTIONS
//=============================================================================

/**
 * @brief Initialize call module
 * @param callback Callback for call state changes
 * @return ESP_OK on success
 */
esp_err_t call_module_init(call_state_callback_t callback);

/**
 * @brief Handle call button event (called by GPIO handler)
 * @param pressed true if button pressed, false if released
 */
void call_module_button_event(bool pressed);

/**
 * @brief Handle remote call signal (from network)
 * @param remote_calling true if remote station is calling
 */
void call_module_remote_signal(bool remote_calling);

/**
 * @brief Get current call state
 * @return Current call state
 */
call_state_t call_module_get_state(void);

/**
 * @brief Check if currently calling
 * @return true if calling (outgoing or acknowledged)
 */
bool call_module_is_calling(void);

/**
 * @brief Check if being called
 * @return true if being called (incoming or acknowledged)
 */
bool call_module_is_being_called(void);

/**
 * @brief Clear call (hang up)
 */
void call_module_clear(void);

#endif // CALL_MODULE_H