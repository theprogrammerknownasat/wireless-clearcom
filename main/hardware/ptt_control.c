/**
 * @file ptt_control.c
 * @brief PTT State Machine Implementation
 */

#include "ptt_control.h"
#include "../config.h"
#include "esp_log.h"

static const char *TAG = "PTT";

//=============================================================================
// PRIVATE VARIABLES
//=============================================================================

static ptt_state_t current_state = PTT_IDLE;
static ptt_state_callback_t user_callback = NULL;
static bool button_currently_pressed = false;

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

    if (pressed) {
        // Button pressed
        if (current_state == PTT_IDLE) {
            // From idle: enter latched on rising edge
            set_state(PTT_LATCHED);
        }
        else if (current_state == PTT_LATCHED) {
            // From latched: check if held long enough for momentary
            if (hold_time_ms >= PTT_HOLD_THRESHOLD_MS) {
                set_state(PTT_MOMENTARY);
            }
        }
        // If already in momentary, stay in momentary
    }
    else {
        // Button released
        if (current_state == PTT_MOMENTARY) {
            // Release from momentary: always go to idle
            set_state(PTT_IDLE);
        }
        else if (current_state == PTT_LATCHED) {
            // Quick release from latched: toggle off
            set_state(PTT_IDLE);
        }
        // If idle, stay idle
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