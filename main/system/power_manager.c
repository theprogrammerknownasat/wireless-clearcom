/**
 * @file power_manager.c
 * @brief Power Management Implementation
 */

#include "power_manager.h"
#include "../config.h"

#if DEVICE_TYPE_PACK

#include "esp_sleep.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "driver/rtc_io.h"

static const char *TAG = "POWER";

static power_state_t current_state = POWER_STATE_ACTIVE;
static power_state_callback_t user_callback = NULL;
static int64_t last_activity_time = 0;

static void set_state(power_state_t new_state)
{
    if (current_state != new_state) {
        current_state = new_state;
        if (user_callback) {
            user_callback(new_state);
        }
    }
}

esp_err_t power_manager_init(power_state_callback_t callback)
{
    user_callback = callback;
    current_state = POWER_STATE_ACTIVE;
    last_activity_time = esp_timer_get_time() / 1000;

    // Configure GPIO wake sources for deep sleep
    // Both PTT and CALL buttons can wake from deep sleep
    // Use ext1 to support multiple GPIO wake sources
    uint64_t wake_mask = (1ULL << BUTTON_PTT_PIN) | (1ULL << BUTTON_CALL_PIN);
    esp_sleep_enable_ext1_wakeup(wake_mask, ESP_EXT1_WAKEUP_ANY_LOW);

    // For light sleep: GPIO wake + WiFi wake
    gpio_wakeup_enable(BUTTON_PTT_PIN, GPIO_INTR_LOW_LEVEL);
    gpio_wakeup_enable(BUTTON_CALL_PIN, GPIO_INTR_LOW_LEVEL);
    esp_sleep_enable_gpio_wakeup();
    esp_sleep_enable_wifi_wakeup();

    ESP_LOGI(TAG, "Power manager initialized (light=%ds, deep=%dmin)",
             LIGHT_SLEEP_TIMEOUT_SEC, DEEP_SLEEP_TIMEOUT_MIN);

    return ESP_OK;
}

void power_manager_activity(void)
{
    last_activity_time = esp_timer_get_time() / 1000;

    if (current_state != POWER_STATE_ACTIVE) {
        set_state(POWER_STATE_ACTIVE);
    }
}

void power_manager_check_timeout(bool *light_sleep, bool *deep_sleep)
{
    int64_t now = esp_timer_get_time() / 1000;
    int64_t idle_time = now - last_activity_time;

    if (light_sleep) {
        *light_sleep = ENABLE_LIGHT_SLEEP &&
                       (idle_time >= (int64_t)LIGHT_SLEEP_TIMEOUT_SEC * 1000);
    }
    if (deep_sleep) {
        *deep_sleep = ENABLE_DEEP_SLEEP &&
                      (idle_time >= (int64_t)DEEP_SLEEP_TIMEOUT_MIN * 60 * 1000);
    }
}

esp_err_t power_manager_enter_light_sleep(void)
{
    set_state(POWER_STATE_LIGHT_SLEEP);

    // Light sleep keeps WiFi connected (auto light sleep)
    // CPU halts but wakes on: button press, WiFi packet, or timer
    esp_light_sleep_start();

    // Woke up
    set_state(POWER_STATE_ACTIVE);
    power_manager_activity();

    return ESP_OK;
}

esp_err_t power_manager_enter_deep_sleep(void)
{
    ESP_LOGI(TAG, "Entering deep sleep (wake: PTT or CALL button)");
    set_state(POWER_STATE_DEEP_SLEEP);

    // Deep sleep - device resets on wake
    esp_deep_sleep_start();

    return ESP_OK;  // Never reached
}

power_state_t power_manager_get_state(void)
{
    return current_state;
}

uint32_t power_manager_get_idle_time(void)
{
    int64_t now = esp_timer_get_time() / 1000;
    return (uint32_t)(now - last_activity_time);
}

#endif // DEVICE_TYPE_PACK
