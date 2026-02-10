/**
 * @file call_module.c
 * @brief Call Signaling Implementation
 */

#include "call_module.h"
#include "../config.h"
#include "esp_log.h"

static const char *TAG = "CALL";

//=============================================================================
// PRIVATE VARIABLES
//=============================================================================

static call_state_t current_state = CALL_IDLE;
static call_state_callback_t user_callback = NULL;
static bool local_calling = false;
static bool remote_calling = false;

//=============================================================================
// PRIVATE FUNCTIONS
//=============================================================================

static void update_call_state(void)
{
    call_state_t new_state;

    if (local_calling && remote_calling) {
        // Both sides calling - acknowledged
        new_state = CALL_ACKNOWLEDGED;
    }
    else if (local_calling && !remote_calling) {
        // We're calling, no response yet
        new_state = CALL_OUTGOING;
    }
    else if (!local_calling && remote_calling) {
        // Remote is calling us
        new_state = CALL_INCOMING;
    }
    else {
        // No call active
        new_state = CALL_IDLE;
    }

    if (current_state != new_state) {
        ESP_LOGI(TAG, "Call state: %s -> %s",
                current_state == CALL_IDLE ? "IDLE" :
                current_state == CALL_OUTGOING ? "OUTGOING" :
                current_state == CALL_INCOMING ? "INCOMING" : "ACKNOWLEDGED",
                new_state == CALL_IDLE ? "IDLE" :
                new_state == CALL_OUTGOING ? "OUTGOING" :
                new_state == CALL_INCOMING ? "INCOMING" : "ACKNOWLEDGED");

        current_state = new_state;

        if (user_callback) {
            user_callback(new_state, local_calling);
        }
    }
}

//=============================================================================
// PUBLIC FUNCTIONS
//=============================================================================

esp_err_t call_module_init(call_state_callback_t callback)
{
    ESP_LOGI(TAG, "Initializing call module...");

    user_callback = callback;
    current_state = CALL_IDLE;
    local_calling = false;
    remote_calling = false;

    ESP_LOGI(TAG, "Call module initialized");

    return ESP_OK;
}

void call_module_button_event(bool pressed)
{
    if (pressed) {
        // Button pressed - initiate call
        ESP_LOGI(TAG, "Call button pressed");
        local_calling = true;
    } else {
        // Button released - clear call
        ESP_LOGI(TAG, "Call button released");
        local_calling = false;
    }

    update_call_state();
}

void call_module_remote_signal(bool remote_calling_new)
{
    if (remote_calling != remote_calling_new) {
        ESP_LOGI(TAG, "Remote call signal: %s", remote_calling_new ? "ON" : "OFF");
        remote_calling = remote_calling_new;
        update_call_state();
    }
}

call_state_t call_module_get_state(void)
{
    return current_state;
}

bool call_module_is_calling(void)
{
    return (current_state == CALL_OUTGOING || current_state == CALL_ACKNOWLEDGED);
}

bool call_module_is_being_called(void)
{
    return (current_state == CALL_INCOMING || current_state == CALL_ACKNOWLEDGED);
}

void call_module_clear(void)
{
    ESP_LOGI(TAG, "Call cleared");
    local_calling = false;
    remote_calling = false;
    update_call_state();
}