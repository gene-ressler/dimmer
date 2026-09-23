/** @brief 0-10 volt wireless dimmer app main. */

#include "encoder.h"
#include "esp_log.h"
#include "led.h"
#include "pwm.h"
#include "shared.h"
#include "strap.h"
#include "wifi.h"

#define RECV_STRAP_GPIO 19
#define LEVEL_SW_GPIO 21              // Rotary encoder push switch
#define LEVEL_CLK_GPIO 22             // Rotary encoder quadrature signal
#define LEVEL_DT_GPIO 23              // Rotary encoder quadrature signal
#define CTRL_OUT_GPIO 25              // PWM output
#define LEVEL_MAX 64                  // Rotary quadrature transitions for 0-10 volts
#define STARTUP_SEND_REPEAT_COUNT 15  // Number of broadcast repeats on power up
#define STANDARD_SEND_REPEAT_COUNT 5  // Number of broadcast repeats on level change
#define IS_MODE_XMIT (!IS_MODE_RECV)
#define IS_MODE_RECV IS_STRAP_PRESENT(straps + 0)

static const char tag[] = "dimmer";

static struct led led[1];
static struct level_encoder encoder[1];
static struct pwm pwm[1];
static struct shared shared[1];
static struct strap straps[1];

#pragma pack(push, 1)
struct dimmer_payload {
  uint16_t level_mils;
};
#pragma pack(pop)

/** @brief Broadcast the encoder's current level, repeatedly for reliability. */
static void send_level(struct level_encoder *encoder, uint16_t repeat_count) {
  uint16_t level_mils = get_level_mils(encoder);
  ESP_LOGD(tag, "send=%ux%u", level_mils, repeat_count);
  struct dimmer_payload payload[1] = {{.level_mils = level_mils}};
  send_repeated((uint8_t *)payload, sizeof *payload, repeat_count);
  checkpoint(shared);
}

/** Handles user twist of the level encoder's knob. */
static void on_level_change(struct level_encoder *encoder) {
  set_led_flash_count(led, MILS_TO_FLASH_COUNT(get_level_mils(encoder)));
  send_level(encoder, STANDARD_SEND_REPEAT_COUNT);
}

/** Handles user press/release of the encoder's momentary contact switch. */
static void on_sw_change(struct level_encoder *encoder) {
  if (encoder->sw_value) {  // on release
    send_level(encoder, STANDARD_SEND_REPEAT_COUNT);
  }
}

/** Handles receipt of a payload. */
static void on_receive(const void *data, uint16_t len) {
  if (len != sizeof(struct dimmer_payload)) {
    ESP_LOGE(tag, "payload len %u!=%u", len, sizeof(struct dimmer_payload));
  }
  uint16_t level_mils = ((struct dimmer_payload *)data)->level_mils;
  ESP_LOGD(tag, "receive=%u", level_mils);
  if (IS_MODE_XMIT) {
    // Sync encoder to level sent by a different transmitter.
    set_level_mils(encoder, level_mils);
    return;
  }
  // For receiver, update PWM output.
  set_pwm_duty_mils(pwm, level_mils);
  set_led_flash_count(led, MILS_TO_FLASH_COUNT(level_mils));
  // Checkpoint new level and sequence.
  set_shared_level_mils(shared, level_mils);
  checkpoint(shared);
}

void app_main(void) {
  // Read GPIO pins serving as straps.
  initialize_strap(straps, "recv", RECV_STRAP_GPIO);
  ESP_LOGI(tag, "mode=%s", IS_MODE_XMIT ? "xmit" : "recv");

#ifdef LOG_MAC
  uint8_t mac[6];
  char buf[40];
  get_mac(mac, buf);
  ESP_LOGI(tag, "mac=%s", buf);
#endif

  // Non-volatile storage. Needed by wifi and nvs (persistence).
  ESP_ERROR_CHECK(nvs_flash_init());
  // Wifi and level encoder use shared, checkpointed variables, so initialize first.
  initialize_shared(shared);
  initialize_wifi(shared, on_receive);
  initialize_led(led, "on-board", ONBOARD_LED_GPIO);

  if (IS_MODE_XMIT) {
    initialize_level_encoder(encoder, "level", LEVEL_SW_GPIO, LEVEL_CLK_GPIO, LEVEL_DT_GPIO,
                             LEVEL_MAX, shared, on_level_change, on_sw_change);
    on_level_change(encoder);  // Consider initial level a change.
    start_level_encoder_sense(encoder);
    send_level(encoder, STARTUP_SEND_REPEAT_COUNT);
  } else {
    initialize_pwm_timer();
    uint16_t level_mils = get_shared_level_mils(shared);
    initialize_pwm(pwm, "ctrl", CTRL_OUT_GPIO, level_mils);
    set_led_flash_count(led, MILS_TO_FLASH_COUNT(level_mils));
  }

  for (;;) {
    // TODO: Put watchdogs here.
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
