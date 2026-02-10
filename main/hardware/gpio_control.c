/**
 * @file gpio_control.c
 * @brief GPIO Control Implementation
 */
#include "../config.h"
#include "gpio_control.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "esp_timer.h"

static const char *TAG = "GPIO";

//=============================================================================
// PRIVATE VARIABLES
//=============================================================================

static bool initialized = false;
static ptt_callback_t user_ptt_callback = NULL;
static call_callback_t user_call_callback = NULL;

#if DEVICE_TYPE_PACK
// Button state (pack only)
static bool ptt_pressed = false;
static bool call_pressed = false;
static int64_t ptt_press_time = 0;
#endif

// LED state
static led_state_t led_states[LED_COUNT] = {0};
static uint8_t led_brightness = LED_BRIGHTNESS_PCT;

// LED blink task
static TaskHandle_t led_task_handle = NULL;
static bool led_task_running = false;

//=============================================================================
// LED PIN MAPPING
//=============================================================================

#if DEVICE_TYPE_PACK
static const gpio_num_t led_pins[LED_COUNT] = {
    [LED_POWER] = LED_POWER_PIN,
    [LED_STATUS] = LED_STATUS_PIN,
    [LED_CALL] = LED_CALL_PIN,
    [LED_PTT] = LED_PTT_PIN,
    [LED_RECEIVE] = LED_RECEIVE_PIN,
};

static const bool led_enabled[LED_COUNT] = {
    [LED_POWER] = LED_POWER_ENABLE,
    [LED_STATUS] = LED_STATUS_ENABLE,
    [LED_CALL] = LED_CALL_ENABLE,
    [LED_PTT] = LED_PTT_ENABLE,
    [LED_RECEIVE] = LED_RECEIVE_ENABLE,
};
#else
static const gpio_num_t led_pins[LED_COUNT] = {
    [LED_POWER] = LED_POWER_PIN,
    [LED_STATUS] = LED_STATUS_PIN,
    [LED_CALL] = LED_CALL_PIN,
    [LED_PTT_MIRROR] = LED_PTT_MIRROR_PIN,
};

static const bool led_enabled[LED_COUNT] = {
    [LED_POWER] = LED_POWER_ENABLE,
    [LED_STATUS] = LED_STATUS_ENABLE,
    [LED_CALL] = LED_CALL_ENABLE,
    [LED_PTT_MIRROR] = LED_PTT_MIRROR_ENABLE,
};
#endif

//=============================================================================
// PRIVATE FUNCTIONS - LED Control
//=============================================================================

static void led_set_physical(led_id_t led, bool on)
{
    if (led >= LED_COUNT || !led_enabled[led]) {
        return;
    }

    // Simple on/off for now (could add PWM brightness later)
    gpio_set_level(led_pins[led], on ? 1 : 0);
}

static void led_task(void *arg)
{
    ESP_LOGI(TAG, "LED task started");

    uint32_t tick = 0;

    while (led_task_running) {
        for (int i = 0; i < LED_COUNT; i++) {
            if (!led_enabled[i]) continue;

            switch (led_states[i]) {
                case LED_OFF:
                    led_set_physical(i, false);
                    break;

                case LED_ON:
                    led_set_physical(i, true);
                    break;

                case LED_BLINK_SLOW:
                    // 1Hz = 1000ms period, 50% duty
                    led_set_physical(i, (tick % 1000) < 500);
                    break;

                case LED_BLINK_FAST:
                    // 5Hz = 200ms period, 50% duty
                    led_set_physical(i, (tick % 200) < 100);
                    break;
            }
        }

        tick += 10;
        vTaskDelay(pdMS_TO_TICKS(10));  // 10ms update rate
    }

    ESP_LOGI(TAG, "LED task stopped");
    vTaskDelete(NULL);
}

//=============================================================================
// PRIVATE FUNCTIONS - Button Handling
//=============================================================================

#if DEVICE_TYPE_PACK
static void IRAM_ATTR ptt_isr_handler(void *arg)
{
    // Read button state (assume active low)
    bool pressed = (gpio_get_level(BUTTON_PTT_PIN) == 0);

    // Store state for polling (ISR should be fast)
    ptt_pressed = pressed;
}

static void IRAM_ATTR call_isr_handler(void *arg)
{
    // Read button state (assume active low)
    bool pressed = (gpio_get_level(BUTTON_CALL_PIN) == 0);
    call_pressed = pressed;
}

static void button_monitor_task(void *arg)
{
    ESP_LOGI(TAG, "Button monitor task started");

    bool last_ptt = false;
    bool last_call = false;

    while (1) {
        // Check PTT button
        bool current_ptt = ptt_pressed;

        if (current_ptt != last_ptt) {
            vTaskDelay(pdMS_TO_TICKS(BUTTON_DEBOUNCE_MS));  // Debounce
            current_ptt = ptt_pressed;  // Re-read after debounce

            if (current_ptt != last_ptt) {  // Confirmed change
                if (current_ptt) {
                    // Button pressed
                    ptt_press_time = esp_timer_get_time() / 1000;  // ms
                    if (user_ptt_callback) {
                        user_ptt_callback(true, 0);
                    }
                } else {
                    // Button released
                    if (user_ptt_callback) {
                        int64_t hold_time = (esp_timer_get_time() / 1000) - ptt_press_time;
                        user_ptt_callback(false, (uint32_t)hold_time);
                    }
                }
                last_ptt = current_ptt;
            }
        }

        // Check for PTT hold (while still pressed)
        if (current_ptt && (esp_timer_get_time() / 1000 - ptt_press_time) > PTT_HOLD_THRESHOLD_MS) {
            if (user_ptt_callback) {
                int64_t hold_time = (esp_timer_get_time() / 1000) - ptt_press_time;
                user_ptt_callback(true, (uint32_t)hold_time);  // Notify of hold
            }
            vTaskDelay(pdMS_TO_TICKS(100));  // Don't spam hold notifications
        }

        // Check call button
        bool current_call = call_pressed;

        if (current_call != last_call) {
            vTaskDelay(pdMS_TO_TICKS(BUTTON_DEBOUNCE_MS));  // Debounce
            current_call = call_pressed;

            if (current_call != last_call) {  // Confirmed change
                if (user_call_callback) {
                    user_call_callback(current_call);
                }
                last_call = current_call;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10));  // 10ms polling
    }
}
#endif // DEVICE_TYPE_PACK

//=============================================================================
// PUBLIC FUNCTIONS
//=============================================================================

esp_err_t gpio_control_init(ptt_callback_t ptt_cb, call_callback_t call_cb)
{
    if (initialized) {
        ESP_LOGW(TAG, "GPIO control already initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing GPIO control...");

    user_ptt_callback = ptt_cb;
    user_call_callback = call_cb;

    // Configure LED GPIOs as outputs
    gpio_config_t led_conf = {
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    for (int i = 0; i < LED_COUNT; i++) {
        if (led_enabled[i]) {
            led_conf.pin_bit_mask = (1ULL << led_pins[i]);
            gpio_config(&led_conf);
            gpio_set_level(led_pins[i], 0);  // Start off
            ESP_LOGI(TAG, "LED %d configured on GPIO %d", i, led_pins[i]);
        }
    }

#if DEVICE_TYPE_PACK
    // Configure button GPIOs as inputs with pull-up (active low)
    gpio_config_t btn_conf = {
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_ANYEDGE,
    };

    // PTT button
    btn_conf.pin_bit_mask = (1ULL << BUTTON_PTT_PIN);
    gpio_config(&btn_conf);
    gpio_install_isr_service(0);
    gpio_isr_handler_add(BUTTON_PTT_PIN, ptt_isr_handler, NULL);
    ESP_LOGI(TAG, "PTT button configured on GPIO %d", BUTTON_PTT_PIN);

    // Call button
    btn_conf.pin_bit_mask = (1ULL << BUTTON_CALL_PIN);
    gpio_config(&btn_conf);
    gpio_isr_handler_add(BUTTON_CALL_PIN, call_isr_handler, NULL);
    ESP_LOGI(TAG, "Call button configured on GPIO %d", BUTTON_CALL_PIN);

    // Start button monitor task
    xTaskCreate(button_monitor_task, "btn_monitor", 4096, NULL, 5, NULL);
#endif

    // Start LED task
    led_task_running = true;
    xTaskCreate(led_task, "led_task", 2048, NULL, 3, &led_task_handle);

    // Turn on power LED
    gpio_control_set_led(LED_POWER, LED_ON);

    initialized = true;
    ESP_LOGI(TAG, "GPIO control initialized");

    return ESP_OK;
}

void gpio_control_set_led(led_id_t led, led_state_t state)
{
    if (led >= LED_COUNT) {
        return;
    }

    led_states[led] = state;
}

void gpio_control_set_brightness(uint8_t brightness)
{
    if (brightness > 100) brightness = 100;
    led_brightness = brightness;
    // TODO: Implement PWM brightness control
}

bool gpio_control_is_ptt_pressed(void)
{
#if DEVICE_TYPE_PACK
    return ptt_pressed;
#else
    return false;
#endif
}

bool gpio_control_is_call_pressed(void)
{
#if DEVICE_TYPE_PACK
    return call_pressed;
#else
    return false;
#endif
}

void gpio_control_deinit(void)
{
    if (!initialized) {
        return;
    }

    ESP_LOGI(TAG, "Deinitializing GPIO control...");

    // Stop LED task
    led_task_running = false;
    if (led_task_handle) {
        vTaskDelay(pdMS_TO_TICKS(100));
        led_task_handle = NULL;
    }

    // Turn off all LEDs
    for (int i = 0; i < LED_COUNT; i++) {
        if (led_enabled[i]) {
            gpio_set_level(led_pins[i], 0);
        }
    }

    initialized = false;
    ESP_LOGI(TAG, "GPIO control deinitialized");
}