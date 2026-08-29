#pragma once

#include <stdint.h>

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"

/** Data for a GPIO pin serving as a PWM output. */
struct pwm {
  /** Name of the PWM output for logging. */
  char *name;
  /** GPIO pin for PWM output. */
  gpio_num_t gpio;
  /** Raw internal duty cycle. */
  uint16_t duty;
};

/** @brief Initializes the timer needed for all PWMs. */
void initialize_pwm_timer(void);
/** @brief Initializes a single PWM output and starts it running. */
void initialize_pwm(struct pwm *pwm, char *name, gpio_num_t gpio, uint16_t init_duty_mils);
/** @brief Sets (changes) the duty cycle of a previously initialized PWM. */
void set_pwm_duty_mils(struct pwm *pwm, uint16_t duty_mils);
