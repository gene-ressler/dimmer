#include <stdint.h>
#include <stdio.h>

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_now.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Comment out for receiver code.
// #define XMIT

#define LED_GPIO 2

// Common to xmit and recv

/** Gets the board's MAC address in binary and text form. */
static void get_mac(uint8_t *mac, char *text) {
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  int32_t p = sprintf(text, "%02x", mac[0]);
  for (int32_t i = 1; i < 6; ++i) {
    p += sprintf(text + p, ":%02x", mac[i]);
  }
}

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

static void initialize_led(struct led *led, char *name, gpio_num_t gpio) {
  led->name = name;
  led->gpio = gpio;
  led->led_state = led->last_led_state = 0;
  led->timer = NULL;
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

/** Sets the LED flash count and starts flashing.  */
static void set_led_flash_count(struct led *led, uint8_t count) {
  uint16_t new_last_led_state = 2 * count;
  // Do nothing if no change.
  if (new_last_led_state == led->last_led_state) {
    return;
  }
  // Else we're starting fresh.
  ESP_LOGI(tag, "LED %s: %d", led->name, count);
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

/** Log tag. */
#ifdef XMIT
#define TAG "xmit"
#else
#define TAG "recv"
#endif
static const char *tag = TAG;

#define LEVEL_SW_GPIO 21
#define LEVEL_CLK_GPIO 22
#define LEVEL_DT_GPIO 23
#define CTRL_OUT_GPIO 25  // PWM

/** Data for single dimmer level encoder. */
struct level_encoder {
  char *name;
  StaticTimer_t timer_state[1];
  TimerHandle_t timer;
  int32_t last_state;
  uint8_t level;
};

#define LEVEL_MAX 64
#define LEVEL_MIN 0
#define LEVEL_POLL_MS 3

/** Table mapping last two states to increment implied by quadrature. */
// clang-format off
static const int8_t increment_by_states[] = {
   0, -1,  1,  0, 
   1,  0,  0, -1, 
  -1,  0,  0,  1, 
   0,  1, -1,  0,
};
// clang-format on

/** Returns the input level clamped to valid range. */
static int32_t clamp_level(int32_t level) {
  if (level > LEVEL_MAX) return LEVEL_MAX;
  if (level < LEVEL_MIN) return LEVEL_MIN;
  return level;
}

/** Reads the level encoder state as a 2-bit quantity: 0 - CLK, 1 - DT. */
static int32_t read_level_encoder(void) {
  int32_t clk = gpio_get_level(LEVEL_CLK_GPIO);
  int32_t dt = gpio_get_level(LEVEL_DT_GPIO);
  return (dt << 1) | clk;
}

/** Initializes the level encoder. GPIO must be initialized in advance. */
static void initialize_level_encoder(struct level_encoder *encoder) {
  encoder->name = "dimmer-level";
  encoder->last_state = read_level_encoder();
  encoder->level = LEVEL_MAX;
  encoder->timer = NULL;
}

/** Handles the level sensing polling callback. */
static void level_encoder_sense_callback(TimerHandle_t timer) {
  struct level_encoder *encoder = pvTimerGetTimerID(timer);
  int32_t state = read_level_encoder();
  if (state != encoder->last_state) {
    int32_t states = ((encoder->last_state << 2) | state) & 0xf;
    encoder->level = clamp_level((int32_t)encoder->level + increment_by_states[states]);
    encoder->last_state = state;
    ESP_LOGI(tag, "Level: %d", encoder->level);
  }
}

static void start_level_encoder_sense(struct level_encoder *encoder) {
  ESP_LOGI(tag, "Start level sensing");
  encoder->timer = xTimerCreateStatic(encoder->name, pdMS_TO_TICKS(10), pdTRUE, encoder,
                                      level_encoder_sense_callback, encoder->timer_state);
  xTimerStart(encoder->timer, 0);
}

static const uint8_t broadcast_mac[ESP_NOW_ETH_ALEN] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

static void wifi_send_callback(const uint8_t *mac_addr, esp_now_send_status_t status) {}

static void wifi_recv_callback(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len) {
}

static void configure_comms(void) {
  // Wifi
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());

  wifi_init_config_t cfg[1] = {WIFI_INIT_CONFIG_DEFAULT()};
  ESP_ERROR_CHECK(esp_wifi_init(cfg));
  ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_start());
  ESP_ERROR_CHECK(esp_wifi_set_channel(3, WIFI_SECOND_CHAN_NONE));

  // Esp-now
  ESP_ERROR_CHECK(esp_now_init());

  // Register callbacks
  ESP_ERROR_CHECK(esp_now_register_send_cb(wifi_send_callback));
  ESP_ERROR_CHECK(esp_now_register_recv_cb(wifi_recv_callback));

  // Broadcast peer
  esp_now_peer_info_t peer_info = {0};
  memcpy(peer_info.peer_addr, broadcast_mac, ESP_NOW_ETH_ALEN);
  peer_info.channel = 0;  // Use current Wi-Fi channel
  peer_info.ifidx = WIFI_IF_STA;
  peer_info.encrypt = false;  // Broadcast packets cannot be encrypted

  ESP_ERROR_CHECK(esp_now_add_peer(&peer_info));
}

struct broadcast_payload {
  uint8_t magic[32];
  uint32_t index;
  uint32_t level;
};

void send_level(int32_t level) { esp_err_t err = esp_now_send(broadcast_mac, (uint8_t *)) }

void app_main(void) {
  uint8_t mac[6];
  char buf[40];
  get_mac(mac, buf);
  ESP_LOGI(tag, "mac %s", buf);

  configure_gpio();
  configure_comms();

  struct led led[1];
  initialize_led(led, "on-board", LED_GPIO);

  struct level_encoder encoder[1];
  initialize_level_encoder(encoder);
  start_level_encoder_sense(encoder);

  // Monitor loop.
  for (;;) {
    set_led_flash_count(led, (7 + encoder->level) / 8);
    if (!gpio_get_level(LEVEL_SW_GPIO)) {
      ESP_LOGI(tag, "Press");
    }
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}
