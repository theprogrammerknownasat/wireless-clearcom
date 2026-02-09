/**
 * @file udp_transport.c
 * @brief UDP Audio Packet Transport Implementation
 */

#include "udp_transport.h"
#include "../config.h"
#include "../system/device_manager.h"
#include "lwip/sockets.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "UDP";

//=============================================================================
// PRIVATE VARIABLES
//=============================================================================

static bool initialized = false;
static bool running = false;
static int sock = -1;
static struct sockaddr_in dest_addr;
static TaskHandle_t rx_task_handle = NULL;
static udp_rx_callback_t user_rx_callback = NULL;

// Statistics
static udp_stats_t stats = {0};
static uint32_t tx_sequence = 0;
static uint32_t last_rx_sequence = 0;

//=============================================================================
// PRIVATE FUNCTIONS
//=============================================================================

static void udp_rx_task(void *arg)
{
    ESP_LOGI(TAG, "UDP RX task started");

    uint8_t rx_buffer[UDP_MAX_PACKET_SIZE];
    struct sockaddr_in source_addr;
    socklen_t addr_len = sizeof(source_addr);

    while (running) {
        // Receive packet
        int len = recvfrom(sock, rx_buffer, sizeof(rx_buffer), 0,
                          (struct sockaddr *)&source_addr, &addr_len);

        if (len < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // Timeout - this is normal
                vTaskDelay(pdMS_TO_TICKS(1));
                continue;
            }
            ESP_LOGE(TAG, "recvfrom failed: errno %d", errno);
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        if (len == 0) {
            continue;
        }

        // Parse packet
        if (len < sizeof(audio_packet_t) - sizeof(((audio_packet_t*)0)->opus_data)) {
            ESP_LOGW(TAG, "Packet too small: %d bytes", len);
            continue;
        }

        audio_packet_t *packet = (audio_packet_t *)rx_buffer;

        // Update statistics
        stats.packets_received++;
        stats.bytes_received += len;

        // Check for lost packets
        if (stats.packets_received > 1) {
            uint32_t expected_seq = last_rx_sequence + 1;
            if (packet->sequence > expected_seq) {
                uint32_t lost = packet->sequence - expected_seq;
                stats.packets_lost += lost;
                ESP_LOGD(TAG, "Lost %lu packets (seq %lu -> %lu)",
                        (unsigned long)lost, (unsigned long)last_rx_sequence,
                        (unsigned long)packet->sequence);
            }
        }
        last_rx_sequence = packet->sequence;

        // Calculate packet loss percentage
        uint32_t total = stats.packets_received + stats.packets_lost;
        if (total > 0) {
            stats.packet_loss_percent = (float)stats.packets_lost / total * 100.0f;
        }

        // Extract flags
        bool ptt_active = (packet->flags & PACKET_FLAG_PTT) != 0;
        bool call_active = (packet->flags & PACKET_FLAG_CALL) != 0;

        // Notify device manager we received a packet (for sleep timeout)
        device_manager_packet_received();

        // Call user callback
        if (user_rx_callback && packet->opus_size > 0) {
            user_rx_callback(packet->opus_data, packet->opus_size,
                           ptt_active, call_active);
        }

        ESP_LOGD(TAG, "RX: seq=%lu, size=%u, ptt=%d, call=%d",
                (unsigned long)packet->sequence, packet->opus_size,
                ptt_active, call_active);
    }

    ESP_LOGI(TAG, "UDP RX task stopped");
    vTaskDelete(NULL);
}

//=============================================================================
// PUBLIC FUNCTIONS
//=============================================================================

esp_err_t udp_transport_init(udp_rx_callback_t rx_callback)
{
    if (initialized) {
        ESP_LOGW(TAG, "UDP transport already initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing UDP transport...");

    user_rx_callback = rx_callback;

    // Create UDP socket
    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        ESP_LOGE(TAG, "Failed to create socket: errno %d", errno);
        return ESP_FAIL;
    }

    // Set socket timeout for RX
    struct timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 100000;  // 100ms timeout
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

#if DEVICE_TYPE_BASE
    // Base station: bind to UDP port to receive from pack
    struct sockaddr_in bind_addr;
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_port = htons(UDP_PORT);
    bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(sock, (struct sockaddr *)&bind_addr, sizeof(bind_addr)) < 0) {
        ESP_LOGE(TAG, "Failed to bind socket: errno %d", errno);
        close(sock);
        sock = -1;
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Bound to port %d", UDP_PORT);

    // Destination: broadcast to connected stations
    // Will be updated when station connects
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(UDP_PORT);
    dest_addr.sin_addr.s_addr = htonl(INADDR_BROADCAST);

#else // DEVICE_TYPE_PACK
    // Belt pack: bind to any port, send to base station
    struct sockaddr_in bind_addr;
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_port = htons(UDP_PORT);
    bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(sock, (struct sockaddr *)&bind_addr, sizeof(bind_addr)) < 0) {
        ESP_LOGE(TAG, "Failed to bind socket: errno %d", errno);
        close(sock);
        sock = -1;
        return ESP_FAIL;
    }

    // Destination: base station
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(UDP_PORT);
    inet_pton(AF_INET, BASE_STATION_IP, &dest_addr.sin_addr);

    ESP_LOGI(TAG, "Target: %s:%d", BASE_STATION_IP, UDP_PORT);
#endif

    // Reset statistics
    memset(&stats, 0, sizeof(stats));
    tx_sequence = 0;
    last_rx_sequence = 0;

    initialized = true;
    ESP_LOGI(TAG, "UDP transport initialized");

    return ESP_OK;
}

esp_err_t udp_transport_start(void)
{
    if (!initialized) {
        ESP_LOGE(TAG, "UDP transport not initialized");
        return ESP_FAIL;
    }

    if (running) {
        ESP_LOGW(TAG, "UDP transport already running");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Starting UDP transport...");

    running = true;

    // Start RX task
    xTaskCreate(udp_rx_task, "udp_rx", 8192, NULL, 4, &rx_task_handle);

    ESP_LOGI(TAG, "UDP transport started");
    return ESP_OK;
}

esp_err_t udp_transport_stop(void)
{
    if (!running) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Stopping UDP transport...");

    running = false;

    // Wait for RX task to finish
    if (rx_task_handle) {
        vTaskDelay(pdMS_TO_TICKS(200));  // Give task time to exit
        rx_task_handle = NULL;
    }

    ESP_LOGI(TAG, "UDP transport stopped");
    return ESP_OK;
}

esp_err_t udp_transport_send(const uint8_t *opus_data, uint16_t opus_size,
                             bool ptt_active, bool call_active)
{
    if (!initialized || sock < 0) {
        return ESP_FAIL;
    }

    if (opus_size > sizeof(((audio_packet_t*)0)->opus_data)) {
        ESP_LOGE(TAG, "Opus data too large: %u bytes", opus_size);
        return ESP_FAIL;
    }

    // Build packet
    audio_packet_t packet;
    packet.sequence = tx_sequence++;
    packet.timestamp = esp_timer_get_time();
    packet.opus_size = opus_size;
    packet.flags = 0;
    if (ptt_active) packet.flags |= PACKET_FLAG_PTT;
    if (call_active) packet.flags |= PACKET_FLAG_CALL;
    packet.reserved = 0;

    if (opus_data && opus_size > 0) {
        memcpy(packet.opus_data, opus_data, opus_size);
    }

    // Calculate packet size (header + opus data)
    size_t packet_size = sizeof(audio_packet_t) - sizeof(packet.opus_data) + opus_size;

    // Send packet
    int sent = sendto(sock, &packet, packet_size, 0,
                     (struct sockaddr *)&dest_addr, sizeof(dest_addr));

    if (sent < 0) {
        // errno 118 = EHOSTUNREACH (no route to host) - WiFi not connected yet
        // errno 113 = EHOSTUNREACH (no route to host) - base station not reachable
        // These are expected during startup/disconnect - don't spam logs
        if (errno != 118 && errno != 113) {
            ESP_LOGE(TAG, "sendto failed: errno %d", errno);
        }
        return ESP_FAIL;
    }

    // Update statistics
    stats.packets_sent++;
    stats.bytes_sent += sent;

    ESP_LOGD(TAG, "TX: seq=%lu, size=%u, ptt=%d, call=%d",
            (unsigned long)packet.sequence, opus_size, ptt_active, call_active);

    return ESP_OK;
}

void udp_transport_get_stats(udp_stats_t *stats_out)
{
    if (stats_out) {
        memcpy(stats_out, &stats, sizeof(udp_stats_t));
    }
}

void udp_transport_reset_stats(void)
{
    memset(&stats, 0, sizeof(stats));
    tx_sequence = 0;
    last_rx_sequence = 0;
}

void udp_transport_deinit(void)
{
    if (!initialized) {
        return;
    }

    ESP_LOGI(TAG, "Deinitializing UDP transport...");

    udp_transport_stop();

    if (sock >= 0) {
        close(sock);
        sock = -1;
    }

    initialized = false;
    ESP_LOGI(TAG, "UDP transport deinitialized");
}