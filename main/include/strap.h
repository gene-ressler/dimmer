#pragma once

#include <stdint.h>

#include "driver/gpio.h"

struct strap {
  char *name;
  uint8_t value;
};

/** @brief Initialize a GPIO pin as a normally 1 strap, pulled to ground for a 0. */
void initialize_strap(struct strap *strap, char *name, gpio_num_t gpio);
