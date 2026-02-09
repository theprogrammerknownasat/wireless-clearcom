/**
 * @file wifi_manager.h
 * @brief WiFi Management for Base Station (AP) and Belt Pack (STA)
 *
 * Handles WiFi initialization, connection, and monitoring.
 */

#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

//=============================================================================
// WIFI EVENTS
//=============================================================================

typedef enum {
    WIFI_EVENT_CONNECTED,       // WiFi connected (STA) or AP started (AP)
    WIFI_EVENT_DISCONNECTED,    // WiFi disconnected (STA only)
    WIFI_EVENT_GOT_IP,          // Got IP address (STA only)
    WIFI_EVENT_STA_JOINED,      // Station connected to AP (AP only)
    WIFI_EVENT_STA_LEFT         // Station disconnected from AP (AP only)
} wifi_event_type_t;

typedef void (*wifi_event_callback_t)(wifi_event_type_t event, void *data);

//=============================================================================
// PUBLIC FUNCTIONS
//=============================================================================

/**
 * @brief Initialize WiFi manager
 * @param event_callback Optional callback for WiFi events
 * @return ESP_OK on success
 */
esp_err_t wifi_manager_init(wifi_event_callback_t event_callback);

/**
 * @brief Start WiFi (AP for base, STA for pack)
 * @return ESP_OK on success
 */
esp_err_t wifi_manager_start(void);

/**
 * @brief Stop WiFi
 * @return ESP_OK on success
 */
esp_err_t wifi_manager_stop(void);

/**
 * @brief Check if WiFi is connected
 * @return true if connected, false otherwise
 */
bool wifi_manager_is_connected(void);

/**
 * @brief Get RSSI (signal strength)
 * @return RSSI in dBm, or 0 if not connected
 */
int8_t wifi_manager_get_rssi(void);

/**
 * @brief Get local IP address
 * @param ip_str Buffer to store IP string (min 16 bytes)
 * @return ESP_OK on success
 */
esp_err_t wifi_manager_get_ip(char *ip_str);

/**
 * @brief Get connected station count (AP mode only)
 * @return Number of connected stations
 */
uint8_t wifi_manager_get_sta_count(void);

/**
 * @brief Deinitialize WiFi manager
 */
void wifi_manager_deinit(void);

#endif // WIFI_MANAGER_H