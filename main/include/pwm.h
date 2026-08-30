#pragma once

/** @brief PWM outputs. */

#include <stdint.h>

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"

/** @brief Data for a GPIO pin serving as a PWM output. */
struct pwm {
  /** @brief Name of the PWM output for logging. */
  char *name;
  /** @brief GPIO pin for PWM output. */
  gpio_num_t gpio;
  /** @brief Raw internal duty cycle. */
  uint16_t duty;
};

/** @brief Initializes the timer needed for all PWMs. */
void initialize_pwm_timer(void);

/**
 * @brief Initializes a single PWM output and starts it running.
 *
 * Sets up the GPIO output pin.
 *
 * @param pwm PWM data to initialize
 * @param name name of the PWM output for logging
 * @param gpio PWM output GPIO pin number
 * @param init_duty_mils initial duty cycle in 1024ths.
 */
void initialize_pwm(struct pwm *pwm, char *name, gpio_num_t gpio, uint16_t init_duty_mils);

/**
 * @brief Sets the duty cycle of a previously initialized PWM.
 *
 * @param pwm PWM data to set
 * @param duty_mils new duty cycle in 1024ths
 */
void set_pwm_duty_mils(struct pwm *pwm, uint16_t duty_mils);
