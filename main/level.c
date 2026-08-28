#include "level.h"

#include <stdint.h>

#include "esp_log.h"

static const char tag[] = "level";

/** Returns the input level clamped to valid range. */
static inline void set_level(struct level_encoder *encoder, int32_t level) {
  // clang-format off
  encoder->level = 
      level > encoder->level_max ? encoder->level_max
    : level < encoder->level_min ? encoder->level_min 
    : level;
  // clang-format on
}

/** Reads the level encoder state as a 2-bit quantity: 0 - CLK, 1 - DT. */
static inline int32_t read_level_encoder(struct level_encoder *encoder) {
  int32_t clk = gpio_get_level(encoder->clk_gpio);
  int32_t dt = gpio_get_level(encoder->dt_gpio);
  return (dt << 1) | clk;
}

#define B(N) (1ULL << (N))

/** Initializes the level encoder. */
void initialize_level_encoder(struct level_encoder *encoder, char *name, uint8_t sw_gpio,
                              uint8_t clk_gpio, uint8_t dt_gpio, uint16_t level_min,
                              uint16_t level_max, uint16_t level_init,
                              void (*on_level_change)(struct level_encoder *),
                              void (*on_sw_change)(struct level_encoder *), void *data) {
  encoder->name = name;
  encoder->sw_gpio = sw_gpio;
  encoder->clk_gpio = clk_gpio;
  encoder->dt_gpio = dt_gpio;
  encoder->level_min = level_min;
  encoder->level_max = level_max;
  encoder->on_level_change = on_level_change;
  encoder->on_sw_change = on_sw_change;
  encoder->data = data;
  set_level(encoder, level_init);
  encoder->last_state = read_level_encoder(encoder);
  encoder->sw_value = gpio_get_level(encoder->sw_gpio);
  encoder->timer = NULL;
  // TODO: Enable pull-ups for  ` ` real encoder.
  gpio_config_t config[1] = {{.pin_bit_mask = B(sw_gpio) | B(clk_gpio) | B(dt_gpio),
                              .mode = GPIO_MODE_INPUT,
                              .pull_up_en = GPIO_PULLUP_DISABLE,
                              .pull_down_en = GPIO_PULLDOWN_DISABLE,
                              .intr_type = GPIO_INTR_DISABLE}};
  gpio_config(config);
}

/** Table mapping last two states to increment implied by quadrature. */
// clang-format off
static const int8_t increment_by_state_pair[] = {
   0, -1,  1,  0, 
   1,  0,  0, -1, 
  -1,  0,  0,  1, 
   0,  1, -1,  0,
};
// clang-format on

/** Handles the level sensing polling callback. */
static void level_encoder_sense_callback(TimerHandle_t timer) {
  struct level_encoder *encoder = pvTimerGetTimerID(timer);
  uint8_t old_sw_value = encoder->sw_value;
  encoder->sw_value = gpio_get_level(encoder->sw_gpio);
  if (encoder->on_sw_change && encoder->sw_value != old_sw_value) {
    encoder->on_sw_change(encoder);
  }
  int32_t state = read_level_encoder(encoder);
  if (state != encoder->last_state) {
    int32_t state_pair = ((encoder->last_state << 2) | state) & 0xf;
    uint8_t old_level = encoder->level;
    set_level(encoder, (int32_t)encoder->level + increment_by_state_pair[state_pair]);
    encoder->last_state = state;
    if (encoder->on_level_change && encoder->level != old_level) {
      encoder->on_level_change(encoder);
    }
  }
}

#define LEVEL_POLL_MS 3

void start_level_encoder_sense(struct level_encoder *encoder) {
  ESP_LOGI(tag, "start sensing");
  encoder->timer = xTimerCreateStatic(encoder->name, pdMS_TO_TICKS(LEVEL_POLL_MS), pdTRUE, encoder,
                                      level_encoder_sense_callback, encoder->timer_state);
  xTimerStart(encoder->timer, 0);
}
