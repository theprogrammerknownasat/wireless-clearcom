/**
 * @file ptt_control.c
 * @brief PTT State Machine Implementation
 */

#include "../config.h"
#include "ptt_control.h"

#if DEVICE_TYPE_PACK

#include "esp_log.h"

static const char *TAG = "PTT";

//=============================================================================
// PRIVATE VARIABLES
//=============================================================================

static ptt_state_t current_state = PTT_IDLE;
static ptt_state_callback_t user_callback = NULL;
static bool button_currently_pressed = false;
static bool just_latched = false;  // Track if we just entered latch (vs been latched)

//=============================================================================
// PRIVATE FUNCTIONS
//=============================================================================

static void set_state(ptt_state_t new_state)
{
    if (current_state != new_state) {
        ESP_LOGI(TAG, "PTT state: %s -> %s",
                current_state == PTT_IDLE ? "IDLE" :
                current_state == PTT_LATCHED ? "LATCHED" : "MOMENTARY",
                new_state == PTT_IDLE ? "IDLE" :
                new_state == PTT_LATCHED ? "LATCHED" : "MOMENTARY");

        current_state = new_state;

        bool transmitting = (new_state == PTT_LATCHED || new_state == PTT_MOMENTARY);

        if (user_callback) {
            user_callback(new_state, transmitting);
        }
    }
}

//=============================================================================
// PUBLIC FUNCTIONS
//=============================================================================

esp_err_t ptt_control_init(ptt_state_callback_t callback)
{
    ESP_LOGI(TAG, "Initializing PTT control...");

    user_callback = callback;
    current_state = PTT_IDLE;
    button_currently_pressed = false;

    ESP_LOGI(TAG, "PTT control initialized (hold threshold: %d ms)", PTT_HOLD_THRESHOLD_MS);

    return ESP_OK;
}

void ptt_control_button_event(bool pressed, uint32_t hold_time_ms)
{
    button_currently_pressed = pressed;

    // SAFETY CHECK: Log unexpected state transitions for debugging
    if (!pressed && current_state == PTT_IDLE) {
        // Got release event while already idle - shouldn't happen
        ESP_LOGW(TAG, "⚠️  PTT release event while IDLE (ghost activation?)");
        return;  // Ignore spurious release
    }

    if (pressed) {
        // ===== BUTTON PRESSED =====

        ESP_LOGI(TAG, "PTT event: PRESS (state=%s, just_latched=%d)",
                 current_state == PTT_IDLE ? "IDLE" :
                 current_state == PTT_LATCHED ? "LATCHED" : "MOMENTARY",
                 just_latched);

        if (current_state == PTT_IDLE) {
            // First press: Enter LATCHED mode
            set_state(PTT_LATCHED);
            just_latched = true;  // Mark that we just entered latch mode
        }
        else if (current_state == PTT_LATCHED) {
            // Already latched: second press
            // Will unlatch on quick release, or become momentary if held
            just_latched = false;  // This is the unlatch press
        }
        else if (current_state == PTT_MOMENTARY) {
            // Already in momentary, stay there
        }
    }
    else {
        // ===== BUTTON RELEASED =====

        ESP_LOGI(TAG, "PTT event: RELEASE (hold_time=%lums, state=%s, just_latched=%d)",
                 (unsigned long)hold_time_ms,
                 current_state == PTT_IDLE ? "IDLE" :
                 current_state == PTT_LATCHED ? "LATCHED" : "MOMENTARY",
                 just_latched);

        if (current_state == PTT_IDLE) {
            // Released while idle: stay idle (already handled above)
        }
        else if (current_state == PTT_LATCHED) {
            // Released while latched: check what type of press this was
            if (hold_time_ms >= PTT_HOLD_THRESHOLD_MS) {
                // Long hold: user tried momentary, unlatch
                ESP_LOGI(TAG, "Long hold detected, unlatching");
                set_state(PTT_IDLE);
                just_latched = false;
            } else {
                // Quick release
                if (just_latched) {
                    // This was the LATCH press - stay latched!
                    ESP_LOGI(TAG, "First quick press - staying LATCHED");
                    just_latched = false;  // Clear flag (now we're "been latched")
                    // Stay in LATCHED state (don't call set_state)
                } else {
                    // This was the UNLATCH press - go to idle
                    ESP_LOGI(TAG, "Second quick press - UNLATCHING");
                    set_state(PTT_IDLE);
                }
            }
        }
        else if (current_state == PTT_MOMENTARY) {
            // Released from momentary: go back to idle
            set_state(PTT_IDLE);
            just_latched = false;
        }
    }
}

ptt_state_t ptt_control_get_state(void)
{
    return current_state;
}

bool ptt_control_is_transmitting(void)
{
    return (current_state == PTT_LATCHED || current_state == PTT_MOMENTARY);
}

void ptt_control_force_idle(void)
{
    ESP_LOGW(TAG, "⚠️  PTT FORCE RESET TO IDLE (was %s)",
             current_state == PTT_IDLE ? "IDLE" :
             current_state == PTT_LATCHED ? "LATCHED" : "MOMENTARY");

    current_state = PTT_IDLE;
    just_latched = false;
    button_currently_pressed = false;

    // Notify callback
    if (user_callback) {
        user_callback(PTT_IDLE, false);
    }
}
#endif // DEVICE_TYPE_PACK