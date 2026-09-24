/**
 * @file
 * @brief Configuration strap implementation.
 */
#include "strap.h"

#include "esp_log.h"

static const char tag[] = "strap";

void initialize_strap(struct strap *strap, char *name, gpio_num_t gpio) {
  strap->name = name;
  gpio_config_t config[1] = {{.pin_bit_mask = (1ULL << gpio),
                              .mode = GPIO_MODE_INPUT,
                              .pull_up_en = GPIO_PULLUP_ENABLE,
                              .pull_down_en = GPIO_PULLDOWN_DISABLE,
                              .intr_type = GPIO_INTR_DISABLE}};
  gpio_config(config);
  strap->value = gpio_get_level(gpio);
  ESP_LOGI(tag, "%s=%s", strap->name, strap->value ? "absent" : "present");
}
