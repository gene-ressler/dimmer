#pragma once

/** @brief Wifi broadcast and receive. */

#include <stdint.h>

#include "shared.h"

/**
 * @brief Returns the device's MAC address as hex digits and text.
 *
 * @param mac 6-byte buffer for MAC address
 * @param text 18-character buffer for MAC address text including final null
 */
void get_mac(uint8_t *mac, char *text);

/**
 * @brief Configures the ESP32's wifi stack for ESP-NOW broadcasts.
 *
 * @param shared shared, checkpointed state variables containing broadcast sequence numbers
 * @param on_receive callback for valid broadcasts
 */
void initialize_wifi(struct shared *shared, void (*on_receive)(const void *data, uint16_t len));

/**
 * @brief Wraps given data in a payload and broadcasts it repeatedly.
 *
 * Returns immediately; repeats continue asynchronously. Causes any previous
 * repeats still in progress to be aborted before starting the new sequence.
 */
void send_repeated(void *data, uint16_t len, uint16_t count);
