#include "encoder.h"

#include "esp_log.h"

static const char tag[] = "encoder";

#define LEVEL_POLL_MS 5

/** @brief Converts raw level to mils with 32-bit signed arithmetic. */
#define TO_LEVEL_MILS(E, L) (1024 * (int32_t)(L) / (E)->level_max)
/** @brief Converts mils to a raw level with 32-bit signed arithmetic. */
#define TO_RAW_LEVEL(E, M) ((E)->level_max * (int32_t)(M) / 1024)

/** Reads the encoder device state as a 2-bit quantity: DT|CLK. */
static inline int32_t read_level_encoder(struct level_encoder *encoder) {
  int32_t clk = gpio_get_level(encoder->clk_gpio);
  int32_t dt = gpio_get_level(encoder->dt_gpio);
  return (dt << 1) | clk;
}

/** @brief Initializes the level encoder. */
void initialize_level_encoder(struct level_encoder *encoder, char *name, uint8_t sw_gpio,
                              uint8_t clk_gpio, uint8_t dt_gpio, uint16_t level_max,
                              struct shared *shared,
                              void (*on_level_change)(struct level_encoder *),
                              void (*on_sw_change)(struct level_encoder *)) {
  encoder->shared = shared;
  encoder->name = name;
  encoder->sw_gpio = sw_gpio;
  encoder->clk_gpio = clk_gpio;
  encoder->dt_gpio = dt_gpio;
  encoder->level_max = level_max;
  encoder->on_level_change = on_level_change;
  encoder->on_sw_change = on_sw_change;
  encoder->timer = NULL;

  encoder->last_state = read_level_encoder(encoder);
  encoder->sw_value = gpio_get_level(encoder->sw_gpio);
  // encoder->level_max must be valid.
  encoder->level = TO_RAW_LEVEL(encoder, get_shared_level_mils(shared));

#define B(N) (1ULL << (N))
  gpio_config_t config[1] = {{.pin_bit_mask = B(sw_gpio) | B(clk_gpio) | B(dt_gpio),
                              .mode = GPIO_MODE_INPUT,
                              .pull_up_en = GPIO_PULLUP_ENABLE,
                              .pull_down_en = GPIO_PULLDOWN_DISABLE,
                              .intr_type = GPIO_INTR_DISABLE}};
#undef B
  gpio_config(config);
}

void set_level_mils(struct level_encoder *encoder, uint16_t level_mils) {
  set_shared_level_mils(encoder->shared, level_mils);
  // A racing thread might overwrite the setting above. Use the final value.
  encoder->level = TO_RAW_LEVEL(encoder, get_shared_level_mils(encoder->shared));
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
    uint16_t old_level = encoder->level;
    int32_t state_pair = ((encoder->last_state << 2) | state) & 0xf;
    int32_t new_level = old_level + increment_by_state_pair[state_pair];
    if (set_shared_level_mils(encoder->shared, TO_LEVEL_MILS(encoder, new_level))) {
      encoder->level = new_level;
      encoder->on_level_change(encoder);
    }
    encoder->last_state = state;
  }
}

void start_level_encoder_sense(struct level_encoder *encoder) {
  ESP_LOGI(tag, "start sense");
  encoder->timer = xTimerCreateStatic(encoder->name, pdMS_TO_TICKS(LEVEL_POLL_MS), pdTRUE, encoder,
                                      level_encoder_sense_callback, encoder->timer_state);
  xTimerStart(encoder->timer, 0);
}
