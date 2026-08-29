#pragma once

#include <stdint.h>

void get_mac(uint8_t *mac, char *text);
void configure_wifi(void (*on_receive)(const void *data, uint16_t len));
void send(uint8_t *data, uint16_t len);
void send_repeated(uint8_t *data, uint16_t len, uint16_t count);
