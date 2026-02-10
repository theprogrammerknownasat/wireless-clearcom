/**
 * @file device_manager.h
 * @brief Device Management and State Machine
 *
 * Manages device identity, pairing, and overall system state.
 */

#ifndef DEVICE_MANAGER_H
#define DEVICE_MANAGER_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

// Include PTT control for state types
#include "../hardware/ptt_control.h"

//=============================================================================
// DEVICE STATE DEFINITIONS
//=============================================================================

typedef enum {
    DEVICE_STATE_INIT,           // Initializing
    DEVICE_STATE_SELF_TEST,      // Running self-test
    DEVICE_STATE_CONNECTING,     // Connecting to network
    DEVICE_STATE_CONNECTED,      // Connected and operational
    DEVICE_STATE_DISCONNECTED,   // Lost connection
    DEVICE_STATE_ERROR,          // Fatal error state
    DEVICE_STATE_SLEEP           // Low power mode
} device_state_t;

//=============================================================================
// PTT STATE DEFINITIONS
//=============================================================================

//=============================================================================
// DEVICE INFO STRUCTURE
//=============================================================================

typedef struct {
    // Device identity
    uint8_t device_id;           // This device's ID
    uint8_t paired_device_id;    // Paired device ID
    bool is_base_station;        // true if base, false if pack

    // Current state
    device_state_t state;        // Overall device state
    ptt_state_t ptt_state;       // PTT button state (pack only)
    bool call_active;            // Call button state

    // Network status
    bool wifi_connected;         // WiFi connection status
    int8_t rssi;                 // Signal strength (dBm)
    uint32_t packets_sent;       // Total packets transmitted
    uint32_t packets_received;   // Total packets received
    uint32_t packets_lost;       // Total packets lost

    // Battery status (pack only)
    float battery_voltage;       // Current battery voltage
    uint8_t battery_percent;     // Estimated battery percentage
    bool battery_low;            // Low battery warning flag
    bool battery_critical;       // Critical battery flag

    // Timing
    int64_t uptime_ms;           // System uptime in milliseconds
    int64_t last_packet_time;    // Time of last received packet

} device_info_t;

//=============================================================================
// PUBLIC FUNCTIONS
//=============================================================================

/**
 * @brief Initialize device manager
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t device_manager_init(void);

/**
 * @brief Get current device information
 * @return Pointer to device info structure
 */
device_info_t* device_manager_get_info(void);

/**
 * @brief Set device state
 * @param state New device state
 */
void device_manager_set_state(device_state_t state);

/**
 * @brief Get current device state
 * @return Current device state
 */
device_state_t device_manager_get_state(void);

/**
 * @brief Set PTT state (belt pack only)
 * @param state New PTT state
 */
void device_manager_set_ptt_state(ptt_state_t state);

/**
 * @brief Get current PTT state
 * @return Current PTT state
 */
ptt_state_t device_manager_get_ptt_state(void);

/**
 * @brief Check if device is transmitting
 * @return true if transmitting, false otherwise
 */
bool device_manager_is_transmitting(void);

/**
 * @brief Set call button state
 * @param active true if call button pressed, false otherwise
 */
void device_manager_set_call_active(bool active);

/**
 * @brief Update WiFi connection status and RSSI
 * @param connected true if connected, false otherwise
 * @param rssi Signal strength in dBm
 */
void device_manager_update_wifi(bool connected, int8_t rssi);

/**
 * @brief Check if call is active
 * @return true if call active, false otherwise
 */
bool device_manager_is_call_active(void);

/**
 * @brief Update network statistics
 * @param packets_sent Number of packets sent
 * @param packets_received Number of packets received
 * @param packets_lost Number of packets lost
 * @param rssi Signal strength in dBm
 */
void device_manager_update_network_stats(uint32_t packets_sent,
                                         uint32_t packets_received,
                                         uint32_t packets_lost,
                                         int8_t rssi);

/**
 * @brief Update battery status (belt pack only)
 * @param voltage Current battery voltage
 */
void device_manager_update_battery(float voltage);

/**
 * @brief Record packet reception (for sleep timeout)
 */
void device_manager_packet_received(void);

/**
 * @brief Check if device should enter sleep mode
 * @return true if sleep timeout reached, false otherwise
 */
bool device_manager_should_sleep(void);

/**
 * @brief Get uptime in seconds
 * @return Uptime in seconds
 */
uint32_t device_manager_get_uptime_sec(void);

/**
 * @brief Print device status to log
 */
void device_manager_print_status(void);

#endif // DEVICE_MANAGER_H