/**
 * @file device_manager.c
 * @brief Device Management and State Machine Implementation
 */

#include "device_manager.h"
#include "../config.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <string.h>
#include <math.h>

static const char *TAG = "DEVICE_MGR";

//=============================================================================
// PRIVATE VARIABLES
//=============================================================================

static device_info_t device_info;
static bool initialized = false;

//=============================================================================
// PRIVATE FUNCTIONS
//=============================================================================

#if DEVICE_TYPE_PACK
/**
 * @brief Calculate battery percentage from voltage
 * @param voltage Battery voltage
 * @return Battery percentage (0-100)
 */
static uint8_t calculate_battery_percent(float voltage)
{
    // LiPo discharge curve (non-linear)
    // These values are approximate for a typical 3.7V LiPo
    if (voltage >= BATTERY_FULL_VOLTAGE) {
        return 100;
    } else if (voltage <= BATTERY_EMPTY_VOLTAGE) {
        return 0;
    }

    // Simple linear approximation between full and empty
    // For production, use a lookup table for more accuracy
    float range = BATTERY_FULL_VOLTAGE - BATTERY_EMPTY_VOLTAGE;
    float position = voltage - BATTERY_EMPTY_VOLTAGE;
    uint8_t percent = (uint8_t)((position / range) * 100.0f);

    // Clamp to 0-100
    if (percent > 100) percent = 100;

    return percent;
}
#endif // DEVICE_TYPE_PACK

//=============================================================================
// PUBLIC FUNCTIONS
//=============================================================================

esp_err_t device_manager_init(void)
{
    if (initialized) {
        ESP_LOGW(TAG, "Device manager already initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing device manager");

    // Clear device info structure
    memset(&device_info, 0, sizeof(device_info_t));

    // Set device identity
    device_info.device_id = DEVICE_ID;

#if DEVICE_TYPE_BASE
    device_info.paired_device_id = PAIRED_PACK_ID;
    device_info.is_base_station = true;
    ESP_LOGI(TAG, "Device type: BASE STATION (ID: 0x%02X)", DEVICE_ID);
    ESP_LOGI(TAG, "Paired with pack ID: 0x%02X", PAIRED_PACK_ID);
#else
    device_info.paired_device_id = PAIRED_BASE_ID;
    device_info.is_base_station = false;
    ESP_LOGI(TAG, "Device type: BELT PACK (ID: 0x%02X)", DEVICE_ID);
    ESP_LOGI(TAG, "Paired with base ID: 0x%02X", PAIRED_BASE_ID);
#endif

    // Set initial state
    device_info.state = DEVICE_STATE_INIT;
    device_info.ptt_state = PTT_IDLE;
    device_info.call_active = false;

    // Initialize timing
    device_info.uptime_ms = 0;
    device_info.last_packet_time = esp_timer_get_time() / 1000; // Convert to ms

    // Initialize battery (pack only)
#if DEVICE_TYPE_PACK
    device_info.battery_voltage = BATTERY_FULL_VOLTAGE;
    device_info.battery_percent = 100;
    device_info.battery_low = false;
    device_info.battery_critical = false;
#endif

    initialized = true;
    ESP_LOGI(TAG, "Device manager initialized successfully");

    return ESP_OK;
}

device_info_t* device_manager_get_info(void)
{
    if (!initialized) {
        ESP_LOGE(TAG, "Device manager not initialized!");
        return NULL;
    }

    // Update uptime
    device_info.uptime_ms = esp_timer_get_time() / 1000;

    return &device_info;
}

void device_manager_set_state(device_state_t state)
{
    if (!initialized) return;

    if (device_info.state != state) {
        ESP_LOGI(TAG, "State transition: %d -> %d", device_info.state, state);
        device_info.state = state;
    }
}

device_state_t device_manager_get_state(void)
{
    if (!initialized) return DEVICE_STATE_ERROR;
    return device_info.state;
}

void device_manager_set_ptt_state(ptt_state_t state)
{
    if (!initialized) return;

#if DEVICE_TYPE_PACK
    // Just update local copy - ptt_control is the source of truth
    if (device_info.ptt_state != state) {
        ESP_LOGD(TAG, "PTT state: %s -> %s",
                device_info.ptt_state == PTT_IDLE ? "IDLE" :
                device_info.ptt_state == PTT_LATCHED ? "LATCHED" : "MOMENTARY",
                state == PTT_IDLE ? "IDLE" :
                state == PTT_LATCHED ? "LATCHED" : "MOMENTARY");
        device_info.ptt_state = state;
    }
#endif
}

ptt_state_t device_manager_get_ptt_state(void)
{
    if (!initialized) return PTT_IDLE;
    return device_info.ptt_state;
}

bool device_manager_is_transmitting(void)
{
    if (!initialized) return false;

#if DEVICE_TYPE_PACK
    return (device_info.ptt_state == PTT_LATCHED ||
            device_info.ptt_state == PTT_MOMENTARY);
#else
    return false; // Base station doesn't have PTT
#endif
}

void device_manager_set_call_active(bool active)
{
    if (!initialized) return;

    if (device_info.call_active != active) {
        ESP_LOGD(TAG, "Call: %s", active ? "ACTIVE" : "INACTIVE");
        device_info.call_active = active;
    }
}

bool device_manager_is_call_active(void)
{
    if (!initialized) return false;
    return device_info.call_active;
}

void device_manager_update_network_stats(uint32_t packets_sent,
                                         uint32_t packets_received,
                                         uint32_t packets_lost,
                                         int8_t rssi)
{
    if (!initialized) return;

    device_info.packets_sent = packets_sent;
    device_info.packets_received = packets_received;
    device_info.packets_lost = packets_lost;
    device_info.rssi = rssi;
}

void device_manager_update_wifi(bool connected, int8_t rssi)
{
    if (!initialized) return;

    device_info.wifi_connected = connected;
    device_info.rssi = rssi;
}

void device_manager_update_battery(float voltage)
{
    if (!initialized) return;

#if DEVICE_TYPE_PACK
    device_info.battery_voltage = voltage;
    device_info.battery_percent = calculate_battery_percent(voltage);

    // Update warning flags
    bool was_low = device_info.battery_low;
    bool was_critical = device_info.battery_critical;

    device_info.battery_low = (voltage <= BATTERY_LOW_VOLTAGE);
    device_info.battery_critical = (voltage <= BATTERY_CRITICAL_VOLTAGE);

    // Log state changes
    if (device_info.battery_low && !was_low) {
        ESP_LOGW(TAG, "Battery LOW: %.2fV (%d%%)", voltage, device_info.battery_percent);
    }
    if (device_info.battery_critical && !was_critical) {
        ESP_LOGE(TAG, "Battery CRITICAL: %.2fV (%d%%)", voltage, device_info.battery_percent);
    }
#endif
}

void device_manager_packet_received(void)
{
    if (!initialized) return;
    device_info.last_packet_time = esp_timer_get_time() / 1000;
}

bool device_manager_should_sleep(void)
{
    if (!initialized) return false;

#if DEVICE_TYPE_PACK && ENABLE_LIGHT_SLEEP
    int64_t now = esp_timer_get_time() / 1000;
    int64_t idle_time_ms = now - device_info.last_packet_time;
    int64_t threshold_ms = LIGHT_SLEEP_TIMEOUT_SEC * 1000;

    return (idle_time_ms >= threshold_ms);
#else
    return false; // Base station never sleeps
#endif
}

uint32_t device_manager_get_uptime_sec(void)
{
    if (!initialized) return 0;
    int64_t uptime_ms = esp_timer_get_time() / 1000;
    return (uint32_t)(uptime_ms / 1000);
}

void device_manager_print_status(void)
{
    if (!initialized) {
        ESP_LOGE(TAG, "Device manager not initialized");
        return;
    }

    device_info_t *info = device_manager_get_info();
    uint32_t uptime_sec = device_manager_get_uptime_sec();
    uint32_t uptime_min = uptime_sec / 60;
    uint32_t uptime_sec_remainder = uptime_sec % 60;

    ESP_LOGI(TAG, "╔════════════════════════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║ %s - ID: 0x%02X - Uptime: %02d:%02d              ║",
             DEVICE_TYPE_STRING, info->device_id, uptime_min, uptime_sec_remainder);
    ESP_LOGI(TAG, "╠════════════════════════════════════════════════════════════╣");
    ESP_LOGI(TAG, "║ State: %d | WiFi: %s | RSSI: %d dBm                  ║",
             info->state, info->wifi_connected ? "CONN" : "DISC", info->rssi);
    ESP_LOGI(TAG, "║ TX: %6lu | RX: %6lu | Lost: %4lu                    ║",
             (unsigned long)info->packets_sent,
             (unsigned long)info->packets_received,
             (unsigned long)info->packets_lost);

#if DEVICE_TYPE_PACK
    ESP_LOGI(TAG, "║ PTT: %d | Call: %d | Battery: %.2fV (%d%%)            ║",
             info->ptt_state, info->call_active,
             info->battery_voltage, info->battery_percent);
#else
    ESP_LOGI(TAG, "║ Call: %d | Paired Pack: 0x%02X                           ║",
             info->call_active, info->paired_device_id);
#endif

    ESP_LOGI(TAG, "╚════════════════════════════════════════════════════════════╝");
}