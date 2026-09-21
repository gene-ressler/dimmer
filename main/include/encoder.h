#pragma once

/** @brief Rotary level encoders. */

#include <stdatomic.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "shared.h"

/** @brief State of a level encoder. */
struct level_encoder {
  /** @brief Shared state variables. Level lives here. */
  struct shared *shared;
  /** @brief Encoder name for logging. */
  char *name;
  /** @brief GPIO input pins polled for encoder state. */
  uint8_t sw_gpio, clk_gpio, dt_gpio;
  /** @brief Number of quadrature transitions to consider "full on". */
  uint16_t level_max;
  /** @brief Push-button switch state. 1 when not pressed. 0 when pressed. */
  uint8_t sw_value;

  // Private.
  /** @brief Last valid encoded level in quadrature ticks. */
  uint16_t level;
  /** @brief Saved callback for encoder position changes. */
  void (*on_level_change)(struct level_encoder *);
  /** @brief Saved callback for pushbutton press/release. */
  void (*on_sw_change)(struct level_encoder *);
  /** @brief Blink timer. Internal. */
  StaticTimer_t timer_state[1];
  /** @brief Blink timer handle. */
  TimerHandle_t timer;
  /** @brief Two bits storing the last encoder quadrature value. */
  int32_t last_state;
};

/**
 * @brief Initializes level encoder state.
 *
 * Sets up GPIO input pins.
 *
 * Starts with the shared level value, which is assumed to be initialized.
 *
 * @param encoder Encoder state to initialize.
 * @param name Name of the encoder.
 * @param sw_gpio Encoder push-button switch GPIO pin.
 * @param clk_gpio Encoder CLK GPIO pin. Half of quadrature signal.
 * @param dt_gpio Encoder DT GPIO pin. Half of quadrature signal.
 * @param level_max Maximum internal level value in quadrature ticks.
 * @param shared Shared state variable holding encoder level.
 * @param on_level_change Callback invoked when the encoder level changes,
 *                        or NULL to disable the callback.
 * @param on_sw_change Callback invoked when the switch value changes, or NULL
 *                     to disable the callback.
 * @param data Application-defined data available through @p encoder.
 */
void initialize_level_encoder(struct level_encoder *encoder, char *name, uint8_t sw_gpio,
                              uint8_t clk_gpio, uint8_t dt_gpio, uint16_t level_max,
                              struct shared *shared,
                              void (*on_level_change)(struct level_encoder *),
                              void (*on_sw_change)(struct level_encoder *));

/**
 * @brief Starts polling of the encoder for level changes.
 *
 * Thread safe.
 */
void start_level_encoder_sense(struct level_encoder *encoder);

/** Sets encoder level in mils, both shared and internal. Thread safe. */
void set_level_mils(struct level_encoder *encoder, uint16_t level_mils);

inline uint16_t get_level_mils(struct level_encoder *encoder) {
  return get_shared_level_mils(encoder->shared);
}
