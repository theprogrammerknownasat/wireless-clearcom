/**
 * @file wifi_manager.c
 * @brief WiFi Management Implementation
 */

#include "wifi_manager.h"
#include "../config.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
#include <string.h>
#include <rom/ets_sys.h>

static const char *TAG = "WIFI_MGR";

//=============================================================================
// PRIVATE VARIABLES
//=============================================================================

static bool initialized = false;
static bool connected = false;
static wifi_event_callback_t user_callback = NULL;
static esp_netif_t *netif = NULL;
static int8_t current_rssi = 0;
static uint8_t sta_count = 0;

//=============================================================================
// PRIVATE FUNCTIONS - Event Handlers
//=============================================================================

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
#if DEVICE_TYPE_BASE
            case WIFI_EVENT_AP_START:
                ESP_LOGI(TAG, "Access Point started");
                ESP_LOGI(TAG, "SSID: %s (hidden: %s)", WIFI_SSID,
                         WIFI_HIDDEN_SSID ? "yes" : "no");
                connected = true;
                if (user_callback) {
                    user_callback(WIFI_EVENT_CONNECTED, NULL);
                }
                break;

            case WIFI_EVENT_AP_STACONNECTED: {
                wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *)event_data;
                ESP_LOGI(TAG, "Station connected: " MACSTR, MAC2STR(event->mac));
                sta_count++;
                if (user_callback) {
                    user_callback(WIFI_EVENT_STA_JOINED, event_data);
                }
                break;
            }

            case WIFI_EVENT_AP_STADISCONNECTED: {
                wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t *)event_data;
                ESP_LOGI(TAG, "Station disconnected: " MACSTR, MAC2STR(event->mac));
                if (sta_count > 0) sta_count--;
                if (user_callback) {
                    user_callback(WIFI_EVENT_STA_LEFT, event_data);
                }
                break;
            }
#else // DEVICE_TYPE_PACK
            case WIFI_EVENT_STA_START:
                ESP_LOGI(TAG, "WiFi station started, connecting to %s...", WIFI_SSID);
                esp_wifi_connect();
                break;

            case WIFI_EVENT_STA_CONNECTED: {
                wifi_event_sta_connected_t *event = (wifi_event_sta_connected_t *)event_data;
                ESP_LOGI(TAG, "Connected to AP: %s (channel %d)", event->ssid, event->channel);
                connected = true;
                if (user_callback) {
                    user_callback(WIFI_EVENT_CONNECTED, event_data);
                }
                break;
            }

            case WIFI_EVENT_STA_DISCONNECTED: {
                wifi_event_sta_disconnected_t *event = (wifi_event_sta_disconnected_t *)event_data;
                ESP_LOGW(TAG, "Disconnected from AP (reason: %d)", event->reason);
                connected = false;
                current_rssi = 0;

                if (user_callback) {
                    user_callback(WIFI_EVENT_DISCONNECTED, event_data);
                }

                // Auto-reconnect
                ESP_LOGI(TAG, "Attempting to reconnect...");
                vTaskDelay(pdMS_TO_TICKS(WIFI_RECONNECT_DELAY_MS));
                esp_wifi_connect();
                break;
            }
#endif
            default:
                break;
        }
    }

#if DEVICE_TYPE_PACK
    else if (event_base == IP_EVENT) {
        if (event_id == IP_EVENT_STA_GOT_IP) {
            ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
            ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));

            // Get initial RSSI
            wifi_ap_record_t ap_info;
            if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
                current_rssi = ap_info.rssi;
                ESP_LOGI(TAG, "Signal strength: %d dBm", current_rssi);
            }

            if (user_callback) {
                user_callback(WIFI_EVENT_GOT_IP, event_data);
            }
        }
    }
#endif
}

//=============================================================================
// PUBLIC FUNCTIONS
//=============================================================================

esp_err_t wifi_manager_init(wifi_event_callback_t event_callback)
{
    if (initialized) {
        ESP_LOGW(TAG, "WiFi manager already initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing WiFi manager...");

    user_callback = event_callback;

    // Initialize network interface
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

#if DEVICE_TYPE_BASE
    netif = esp_netif_create_default_wifi_ap();
    ESP_LOGI(TAG, "Mode: Access Point");
#else
    netif = esp_netif_create_default_wifi_sta();
    ESP_LOGI(TAG, "Mode: Station");
#endif

    // Initialize WiFi with default config
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // Register event handlers
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                               &wifi_event_handler, NULL));
#if DEVICE_TYPE_PACK
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                               &wifi_event_handler, NULL));
#endif

    initialized = true;
    ESP_LOGI(TAG, "WiFi manager initialized");

    return ESP_OK;
}

esp_err_t wifi_manager_start(void)
{
    if (!initialized) {
        ESP_LOGE(TAG, "WiFi manager not initialized");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Starting WiFi...");

#if DEVICE_TYPE_BASE
    // Configure as Access Point
    wifi_config_t wifi_config = {
        .ap = {
            .ssid = WIFI_SSID,
            .ssid_len = strlen(WIFI_SSID),
            .channel = WIFI_CHANNEL,
            .password = WIFI_PASSWORD,
            .max_connection = MAX_STA_CONN,
            .authmode = WIFI_AUTH_WPA2_PSK,
            .ssid_hidden = WIFI_HIDDEN_SSID,
            .beacon_interval = 100,
            .pmf_cfg = {
                .capable = false,  // Disable PMF
                .required = false
            },
        },
    };

    if (strlen(WIFI_PASSWORD) == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));

#else // DEVICE_TYPE_PACK
    // Configure as Station
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASSWORD,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {
                .capable = false,  // Disable PMF to match AP
                .required = false
            },
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
#endif

    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi started");
    return ESP_OK;
}

esp_err_t wifi_manager_stop(void)
{
    if (!initialized) {
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Stopping WiFi...");
    esp_wifi_stop();
    connected = false;
    current_rssi = 0;
    sta_count = 0;

    return ESP_OK;
}

bool wifi_manager_is_connected(void)
{
    return connected;
}

int8_t wifi_manager_get_rssi(void)
{
#if DEVICE_TYPE_PACK
    if (!connected) {
        return 0;
    }

    // Update RSSI
    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        current_rssi = ap_info.rssi;
    }
#endif

    return current_rssi;
}

esp_err_t wifi_manager_get_ip(char *ip_str)
{
    if (!ip_str || !initialized) {
        return ESP_FAIL;
    }

    esp_netif_ip_info_t ip_info;
    if (esp_netif_get_ip_info(netif, &ip_info) != ESP_OK) {
        strcpy(ip_str, "0.0.0.0");
        return ESP_FAIL;
    }

    sprintf(ip_str, IPSTR, IP2STR(&ip_info.ip));
    return ESP_OK;
}

uint8_t wifi_manager_get_sta_count(void)
{
#if DEVICE_TYPE_BASE
    return sta_count;
#else
    return 0;  // Pack doesn't have connected stations
#endif
}

void wifi_manager_deinit(void)
{
    if (!initialized) {
        return;
    }

    ESP_LOGI(TAG, "Deinitializing WiFi manager...");

    wifi_manager_stop();

    esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler);
#if DEVICE_TYPE_PACK
    esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler);
#endif

    esp_wifi_deinit();

    if (netif) {
        esp_netif_destroy(netif);
        netif = NULL;
    }

    initialized = false;
    connected = false;
    ESP_LOGI(TAG, "WiFi manager deinitialized");
}