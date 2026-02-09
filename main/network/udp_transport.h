/**
 * @file udp_transport.h
 * @brief UDP Audio Packet Transport
 *
 * Handles transmission and reception of Opus-encoded audio packets.
 */

#ifndef UDP_TRANSPORT_H
#define UDP_TRANSPORT_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

//=============================================================================
// PACKET STRUCTURE
//=============================================================================

#define UDP_MAX_PACKET_SIZE  512

typedef struct __attribute__((packed)) {
    uint32_t sequence;           // Incrementing packet number
    uint32_t timestamp;          // Microsecond timestamp
    uint16_t opus_size;          // Size of Opus data
    uint8_t  flags;              // Bit 0: PTT active, Bit 1: Call active
    uint8_t  reserved;           // Future use
    uint8_t  opus_data[256];     // Opus compressed audio
} audio_packet_t;

// Flag bits
#define PACKET_FLAG_PTT   (1 << 0)
#define PACKET_FLAG_CALL  (1 << 1)

//=============================================================================
// STATISTICS
//=============================================================================

typedef struct {
    uint32_t packets_sent;
    uint32_t packets_received;
    uint32_t packets_lost;
    uint32_t bytes_sent;
    uint32_t bytes_received;
    float packet_loss_percent;
} udp_stats_t;

//=============================================================================
// CALLBACK TYPES
//=============================================================================

/**
 * @brief Callback when audio packet is received
 * @param opus_data Opus encoded audio
 * @param opus_size Size of Opus data
 * @param ptt_active Remote PTT state
 * @param call_active Remote call state
 */
typedef void (*udp_rx_callback_t)(const uint8_t *opus_data, uint16_t opus_size,
                                   bool ptt_active, bool call_active);

//=============================================================================
// PUBLIC FUNCTIONS
//=============================================================================

/**
 * @brief Initialize UDP transport
 * @param rx_callback Callback for received packets
 * @return ESP_OK on success
 */
esp_err_t udp_transport_init(udp_rx_callback_t rx_callback);

/**
 * @brief Start UDP transport (begins RX task)
 * @return ESP_OK on success
 */
esp_err_t udp_transport_start(void);

/**
 * @brief Stop UDP transport
 * @return ESP_OK on success
 */
esp_err_t udp_transport_stop(void);

/**
 * @brief Send audio packet
 * @param opus_data Opus encoded audio
 * @param opus_size Size of Opus data
 * @param ptt_active Local PTT state
 * @param call_active Local call state
 * @return ESP_OK on success
 */
esp_err_t udp_transport_send(const uint8_t *opus_data, uint16_t opus_size,
                             bool ptt_active, bool call_active);

/**
 * @brief Get transport statistics
 * @param stats Pointer to stats structure
 */
void udp_transport_get_stats(udp_stats_t *stats);

/**
 * @brief Reset statistics
 */
void udp_transport_reset_stats(void);

/**
 * @brief Deinitialize UDP transport
 */
void udp_transport_deinit(void);

#endif // UDP_TRANSPORT_H