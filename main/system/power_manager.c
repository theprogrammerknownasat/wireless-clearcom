/**
 * @file power_manager.c
 * @brief Power Management Implementation
 */

#include "power_manager.h"
#include "../config.h"

#if DEVICE_TYPE_PACK

#include "esp_sleep.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "driver/rtc_io.h"

static const char *TAG = "POWER";

//=============================================================================
// PRIVATE VARIABLES
//=============================================================================

static power_state_t current_state = POWER_STATE_ACTIVE;
static power_state_callback_t user_callback = NULL;
static int64_t last_activity_time = 0;

//=============================================================================
// PRIVATE FUNCTIONS
//=============================================================================

static void set_state(power_state_t new_state)
{
    if (current_state != new_state) {
        ESP_LOGI(TAG, "Power state: %s -> %s",
                current_state == POWER_STATE_ACTIVE ? "ACTIVE" :
                current_state == POWER_STATE_LIGHT_SLEEP ? "LIGHT_SLEEP" : "DEEP_SLEEP",
                new_state == POWER_STATE_ACTIVE ? "ACTIVE" :
                new_state == POWER_STATE_LIGHT_SLEEP ? "LIGHT_SLEEP" : "DEEP_SLEEP");

        current_state = new_state;

        if (user_callback) {
            user_callback(new_state);
        }
    }
}

//=============================================================================
// PUBLIC FUNCTIONS
//=============================================================================

esp_err_t power_manager_init(power_state_callback_t callback)
{
    ESP_LOGI(TAG, "Initializing power manager...");

    user_callback = callback;
    current_state = POWER_STATE_ACTIVE;
    last_activity_time = esp_timer_get_time() / 1000;  // ms

    // Configure wake sources
    // Wake on PTT button (GPIO)
    esp_sleep_enable_ext0_wakeup(BUTTON_PTT_PIN, 0);  // Wake on low (button press)

    ESP_LOGI(TAG, "Power manager initialized");
    ESP_LOGI(TAG, "Light sleep timeout: %d seconds", LIGHT_SLEEP_TIMEOUT_SEC);
    ESP_LOGI(TAG, "Deep sleep timeout: %d minutes", DEEP_SLEEP_TIMEOUT_MIN);

    return ESP_OK;
}

void power_manager_activity(void)
{
    last_activity_time = esp_timer_get_time() / 1000;  // ms

    // If we were sleeping, wake up
    if (current_state != POWER_STATE_ACTIVE) {
        set_state(POWER_STATE_ACTIVE);
    }
}

void power_manager_check_timeout(bool *light_sleep, bool *deep_sleep)
{
    int64_t now = esp_timer_get_time() / 1000;  // ms
    int64_t idle_time = now - last_activity_time;

    int64_t light_sleep_threshold = LIGHT_SLEEP_TIMEOUT_SEC * 1000;
    int64_t deep_sleep_threshold = DEEP_SLEEP_TIMEOUT_MIN * 60 * 1000;

    if (light_sleep) {
        *light_sleep = (idle_time >= light_sleep_threshold);
    }

    if (deep_sleep) {
        *deep_sleep = (idle_time >= deep_sleep_threshold);
    }
}

esp_err_t power_manager_enter_light_sleep(void)
{
    ESP_LOGI(TAG, "Entering light sleep...");
    ESP_LOGI(TAG, "Wake sources: PTT button, Call button");

    set_state(POWER_STATE_LIGHT_SLEEP);

    // Configure wake sources for light sleep
    // GPIO wake already configured in init

    // Enter light sleep
    esp_light_sleep_start();

    // Wake up!
    ESP_LOGI(TAG, "Woke from light sleep");
    set_state(POWER_STATE_ACTIVE);
    power_manager_activity();  // Reset activity timer

    return ESP_OK;
}

esp_err_t power_manager_enter_deep_sleep(void)
{
    ESP_LOGI(TAG, "Entering deep sleep...");
    ESP_LOGI(TAG, "Wake source: PTT button only");
    ESP_LOGI(TAG, "Device will reset on wake");

    set_state(POWER_STATE_DEEP_SLEEP);

    // Enter deep sleep (does not return - device resets on wake)
    esp_deep_sleep_start();

    // Never reached
    return ESP_OK;
}

power_state_t power_manager_get_state(void)
{
    return current_state;
}

uint32_t power_manager_get_idle_time(void)
{
    int64_t now = esp_timer_get_time() / 1000;  // ms
    return (uint32_t)(now - last_activity_time);
}

#endif // DEVICE_TYPE_PACK