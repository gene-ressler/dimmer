#pragma once

/** @brief Flashing LEDs */

#include <stdint.h>

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"

/** @brief Data for a timer-driven flashing LED. */
struct led {
  /** @brief Name of the LED for logging. */
  char *name;
  /** @brief GPIO pin driving the LED. */
  gpio_num_t gpio;

  // Internal.
  /** @brief Blink timer state. */
  StaticTimer_t timer_state[1];
  /** @brief Blink timer handle. */
  TimerHandle_t timer;
  /**
   * @brief Index connoting which flashing phase the led is in.
   *
   * States:
   * - 0 - long off
   * - 1 - first on
   * - 2 - first short off
   * - ...
   * - last_led_state-1 - last on (followed by 0 = long off)
   */
  uint16_t led_state;
  /** @brief Two times desired_flash_count */
  uint8_t last_led_state;
};

/**
 * @brief Initializes data for a flashing LED.
 *
 * Sets up the GPIO output pin. Initial state is not flashing.
 *
 * @param led `struct led` to be initialized.
 * @param name name of the LED for logging.
 * @param gpio GPIO pin driving the LED.
 */
void initialize_led(struct led *led, char *name, gpio_num_t gpio);

/**
 * @brief Sets LED flash count.
 *
 * Halts any flashing in progress and starts a new sequence.
 * Count of zero turns off the LED completely.
 *
 * @param led initialized data for flashing LED
 * @param count number of times to flash between pauses
 */
void set_led_flash_count(struct led *led, uint8_t count);

/** @brief GPIO pin number of the on-board LED. */
#define ONBOARD_LED_GPIO GPIO_NUM_2
