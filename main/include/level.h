#pragma once

#include <stdint.h>

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"

/** State of a level encoder. */
struct level_encoder {
  char *name;
  uint8_t sw_gpio, clk_gpio, dt_gpio;
  uint16_t level_min, level_max;
  void (*on_level_change)(struct level_encoder *);
  void (*on_sw_change)(struct level_encoder *);
  void *data;
  StaticTimer_t timer_state[1];
  TimerHandle_t timer;
  int32_t last_state;
  uint8_t level;
  uint8_t sw_value;
};

/**
 * Initialize level encoder state. The Elegoo EC11 encoder module produces about 80 level changes
 * per turn.
 */
void initialize_level_encoder(struct level_encoder *encoder, char *name, uint8_t sw_gpio,
                              uint8_t clk_gpio, uint8_t dt_gpio, uint16_t level_min,
                              uint16_t level_max, uint16_t level_init,
                              void (*on_level_change)(struct level_encoder *),
                              void (*on_sw_change)(struct level_encoder *), void *data);
void start_level_encoder_sense(struct level_encoder *encoder);