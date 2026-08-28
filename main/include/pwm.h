#pragma once

#include <stdint.h>

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"

/** Data for a GPIO pin serving as a PWM output. */
struct pwm {
  char *name;
  gpio_num_t gpio;
  uint16_t duty;
};

/** Converts a value in 0..100 to a duty cycle value. Not clamped. */
#define PERCENT_TO_DUTY(P) ((P) * 127 / 100)

void initialize_pwm_timer(void);
void initialize_pwm(struct pwm *pwm, char *name, gpio_num_t gpio, uint16_t init_duty);
void set_pwm_duty(struct pwm *pwm, uint16_t duty);
