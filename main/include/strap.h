#pragma once

/** @brief Configuration straps. */

#include <stdint.h>

#include "driver/gpio.h"

/** @brief  Data for a strap. */
struct strap {
  /** @brief Name of the strap for logging. */
  char *name;
  /** @brief Value of the strap at initialization time. */
  uint8_t value;
};

/**
 * @brief Initializes and polls the value of strap.
 *
 * Sets up the strap GPIO input pin. It is pulled-up to HI (1) when no
 * strap is present. It has value LO (0) only when strapped to ground.
 */
void initialize_strap(struct strap *strap, char *name, gpio_num_t gpio);
