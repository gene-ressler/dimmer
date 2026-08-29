#pragma once

#include <stdint.h>

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"

/** State of a level encoder. */
struct level_encoder {
  /** Encoder name for logging. */
  char *name;
  /** GPIO pins to poll for encoder state. */
  uint8_t sw_gpio, clk_gpio, dt_gpio;
  /**
   * Number of quadrature transitions to consider as "full on".
   * The Elegoo EC11 encoder module produces about 80 per turn.
   */
  uint16_t level_max;
  void (*on_level_change)(struct level_encoder *);
  void (*on_sw_change)(struct level_encoder *);
  void *data;
  StaticTimer_t timer_state[1];
  TimerHandle_t timer;
  int32_t last_state;
  /** Encoder level in 0..level_max. */
  uint8_t level;
  /** Push-button switch state. 1 when not pressed. 0 when pressed. */
  uint8_t sw_value;
};

/**
 * @brief Initializes level encoder state.
 *
 * Note:
 *
 * @param encoder Encoder state to initialize.
 * @param name Name of the encoder.
 * @param sw_gpio Encoder push-button switch GPIO pin.
 * @param clk_gpio Encoder CLK GPIO pin. Half of quadrature signal.
 * @param dt_gpio Encoder DT GPIO pin. Half of quadrature signal.
 * @param level_max Maximum level value.
 * @param level_init Initial level value.
 * @param on_level_change Callback invoked when the encoder level changes,
 *                        or NULL to disable the callback.
 * @param on_sw_change Callback invoked when the switch value changes, or NULL
 *                     to disable the callback.
 * @param data Application-defined data available through @p encoder.
 */
void initialize_level_encoder(struct level_encoder *encoder, char *name, uint8_t sw_gpio,
                              uint8_t clk_gpio, uint8_t dt_gpio, uint16_t level_max,
                              uint16_t level_init, void (*on_level_change)(struct level_encoder *),
                              void (*on_sw_change)(struct level_encoder *));

/** @brief Starts polling of the encoder for level changes. */
void start_level_encoder_sense(struct level_encoder *encoder);

/**
 * @brief Sets the encoder level using thousandths of its configured maximum.
 *
 * @param encoder Encoder whose level should be changed.
 * @param level Level in the range 0..1024.
 */
void set_level_mils(struct level_encoder *encoder, uint16_t level);

/**
 * @brief Gets the current encoder level in thousandths of its maximum.
 *
 * @param encoder Encoder whose current level should be read.
 * @return Current level in the range 0..1024.
 */
uint16_t get_level_mils(struct level_encoder *encoder);
