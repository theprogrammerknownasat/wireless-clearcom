/**
 * @file clearcom_line.h
 * @brief ClearCom Party Line Interface (Base Station Only)
 *
 * Handles audio interface to wired ClearCom party line system.
 * Uses WM8960 line input/output for balanced audio.
 */

#ifndef CLEARCOM_LINE_H
#define CLEARCOM_LINE_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

//=============================================================================
// LINE STATUS
//=============================================================================

typedef struct {
    bool line_connected;     // Party line physically connected
    float input_level;       // Input audio level (RMS)
    float output_level;      // Output audio level (RMS)
    bool dc_offset_detected; // DC offset on input (fault condition)
} clearcom_line_status_t;

//=============================================================================
// PUBLIC FUNCTIONS
//=============================================================================

/**
 * @brief Initialize ClearCom line interface
 * @return ESP_OK on success
 */
esp_err_t clearcom_line_init(void);

/**
 * @brief Start line interface
 * @return ESP_OK on success
 */
esp_err_t clearcom_line_start(void);

/**
 * @brief Stop line interface
 * @return ESP_OK on success
 */
esp_err_t clearcom_line_stop(void);

/**
 * @brief Read audio from party line
 * @param buffer Buffer to store audio samples
 * @param sample_count Number of samples to read
 * @return ESP_OK on success
 */
esp_err_t clearcom_line_read(int16_t *buffer, size_t sample_count);

/**
 * @brief Write audio to party line
 * @param buffer Buffer containing audio samples
 * @param sample_count Number of samples to write
 * @return ESP_OK on success
 */
esp_err_t clearcom_line_write(const int16_t *buffer, size_t sample_count);

/**
 * @brief Get line status
 * @param status Pointer to status structure
 */
void clearcom_line_get_status(clearcom_line_status_t *status);

/**
 * @brief Set line output gain
 * @param gain Gain level (0-31)
 */
void clearcom_line_set_output_gain(uint8_t gain);

/**
 * @brief Set line input gain
 * @param gain Gain level (0-31)
 */
void clearcom_line_set_input_gain(uint8_t gain);

/**
 * @brief Deinitialize line interface
 */
void clearcom_line_deinit(void);

#endif // CLEARCOM_LINE_H