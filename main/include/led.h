#pragma once

#include <stdint.h>

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"

/** Data for a timer-driven flashing LED. */
struct led {
  char *name;
  gpio_num_t gpio;
  StaticTimer_t timer_state[1];
  TimerHandle_t timer;
  // Index connoting which flashing phase the led is in.
  // 0 - long off
  // 1 - first on
  // 2 - first short off
  // ...
  // last_led_state-1 - last on (followed by 0 = long off)
  uint16_t led_state;
  // 2 * desired_flash_count
  uint8_t last_led_state;
};

void initialize_led(struct led *led, char *name, gpio_num_t gpio);
void set_led_flash_count(struct led *led, uint8_t count);

#define ONBOARD_LED_GPIO GPIO_NUM_2
