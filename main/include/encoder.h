/**
 * @file
 * @brief Rotary level encoder.
 */
#pragma once

#include <stdatomic.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "shared.h"

/** @brief State of a level encoder. */
struct level_encoder {
  struct shared *shared;  ///< Shared state variables. Level lives here.
  char *name;             ///< Encoder name for logging.
  uint8_t sw_gpio;        ///< GPIO input pin: push switch.
  uint8_t clk_gpio;       ///< GPIO input pin: quadrature A.
  uint8_t dt_gpio;        ///< GPIO input pin: quadrature B.
  uint16_t level_max;     ///< Number of quadrature transitions to consider "full on".
  uint8_t sw_value;       ///< Push-button switch state. 0=pressed, 1=not.

  // Private.
  uint16_t level;  ///< Last valid encoded level in quadrature ticks. Internal
  void (*on_level_change)(struct level_encoder *);  ///< Encoder position change callback.
  void (*on_sw_change)(struct level_encoder *);     ///< Pushbutton press/release callback.
  StaticTimer_t timer_state[1];                     ///< Blink timer state. Internal.
  TimerHandle_t timer;                              ///< Blink timer. Internal.
  int32_t last_state;                               ///< Last encoder quadrature state. 2 bits.
};

/**
 * @brief Initializes level encoder state.
 *
 * Uses the shared level value, which must be already initialized.
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
 */
void initialize_level_encoder(struct level_encoder *encoder, char *name, uint8_t sw_gpio,
                              uint8_t clk_gpio, uint8_t dt_gpio, uint16_t level_max,
                              struct shared *shared,
                              void (*on_level_change)(struct level_encoder *),
                              void (*on_sw_change)(struct level_encoder *));

/** @brief Starts polling of the encoder for level changes. Thread safe. */
void start_level_encoder_sense(struct level_encoder *encoder);

/** @brief Sets encoder level in mils, both shared and internal. Thread safe. */
void set_level_mils(struct level_encoder *encoder, uint16_t level_mils);

/** @brief Gets encoder level in mils. Thread safe. */
inline uint16_t get_level_mils(struct level_encoder *encoder) {
  return get_shared_level_mils(encoder->shared);
}
