#include "led.h"

#include "esp_log.h"

static const char tag[] = "led";

void initialize_led(struct led *led, char *name, gpio_num_t gpio) {
  led->name = name;
  led->gpio = gpio;
  led->led_state = led->last_led_state = 0;
  led->timer = NULL;
  gpio_config_t config[1] = {{.pin_bit_mask = (1ULL << gpio),
                              .mode = GPIO_MODE_OUTPUT,
                              .pull_up_en = GPIO_PULLUP_DISABLE,
                              .pull_down_en = GPIO_PULLDOWN_DISABLE,
                              .intr_type = GPIO_INTR_DISABLE}};
  gpio_config(config);
}

#define LED_ON_MS 50
#define LED_SHORT_OFF_MS 300
#define LED_LONG_OFF_MS 1000

static void led_timer_callback(TimerHandle_t timer) {
  struct led *led = pvTimerGetTimerID(timer);
  // Turn on LED if new state value is odd.
  uint8_t led_on_state = ++led->led_state & 1;
  TickType_t period = led_on_state ? LED_ON_MS : LED_SHORT_OFF_MS;
  // Wrap LED state if last blink was just completed.
  if (led->led_state == led->last_led_state) {
    led->led_state = 0;
    period = LED_LONG_OFF_MS;
  }
  gpio_set_level(led->gpio, led_on_state);
  xTimerChangePeriod(timer, period, 0);
}

void set_led_flash_count(struct led *led, uint8_t count) {
  uint16_t new_last_led_state = 2 * count;
  // Do nothing if no change.
  if (new_last_led_state == led->last_led_state) {
    return;
  }
  // Else we're starting fresh.
  ESP_LOGD(tag, "%s=%d", led->name, count);
  gpio_set_level(led->gpio, 0);
  led->led_state = 0;
  led->last_led_state = new_last_led_state;
  // Handle user command to stop flashing.
  if (new_last_led_state == 0) {
    if (led->timer) {
      xTimerStop(led->timer, 0);
    }
    return;
  }
  // Set up timer for first flash.
  if (!led->timer) {
    led->timer = xTimerCreateStatic(led->name, pdMS_TO_TICKS(LED_LONG_OFF_MS), pdFALSE, led,
                                    led_timer_callback, led->timer_state);
    xTimerStart(led->timer, 0);
  } else {
    xTimerChangePeriod(led->timer, pdMS_TO_TICKS(LED_LONG_OFF_MS), 0);
  }
}
