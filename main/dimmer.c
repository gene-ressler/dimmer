/**
 * @file
 * @brief Wireless 0-10 volt dimmer app main.
 *
 * Entry point for dimmer firmware. See README.md for details.
 *
 * @author Gene Ressler
 * @date 2026-09-23
 * @version 0.9
 * @copyright Copyright (c) 2026 Gene Ressler. All rights reserved.
 */

#include "encoder.h"
#include "esp_log.h"
#include "led.h"
#include "pwm.h"
#include "shared.h"
#include "strap.h"
#include "wifi.h"

#define RECV_STRAP_GPIO 19            ///< Ground strap for receiver selection gpio pin.
#define LEVEL_SW_GPIO 21              ///< Rotary encoder push switch gpio pin.
#define LEVEL_CLK_GPIO 22             ///< Rotary encoder quadrature signal gpio pin.
#define LEVEL_DT_GPIO 23              ///< Rotary encoder quadrature signal gpio pin.
#define CTRL_OUT_GPIO 25              ///< PWM output gpio pin.
#define LEVEL_MAX 64                  ///< Rotary quadrature clicks for 10 volts.
#define STARTUP_SEND_REPEAT_COUNT 15  ///< Number of broadcast repeats on power up.
#define STANDARD_SEND_REPEAT_COUNT 5  ///< Number of broadcast repeats on level change.

#define IS_MODE_XMIT (!IS_MODE_RECV)           ///< Whether device is strapped for transmit.
#define IS_MODE_RECV IS_STRAP_PRESENT(straps)  ///< Whether device is strapped for receive.

static const char tag[] = "dimmer";

// System objects.
static struct led led[1];
static struct level_encoder encoder[1];
static struct pwm pwm[1];
static struct shared shared[1];
static struct strap straps[1];

/** @brief Packed broadcast data payload. */
#pragma pack(push, 1)
struct dimmer_payload {
  uint16_t level_mils;  ///< Dimming level in [0..1024].
};
#pragma pack(pop)

/** @brief Broadcasts the encoder's current level, repeatedly for reliability. */
static void send_level(struct level_encoder *encoder, uint16_t repeat_count) {
  uint16_t level_mils = get_level_mils(encoder);
  ESP_LOGD(tag, "send=%ux%u", level_mils, repeat_count);
  struct dimmer_payload payload[1] = {{.level_mils = level_mils}};
  send_repeated((uint8_t *)payload, sizeof *payload, repeat_count);
  checkpoint(shared);
}

/** @brief Handles user twist of the level encoder's knob. */
static void on_level_change(struct level_encoder *encoder) {
  set_led_flash_count(led, MILS_TO_FLASH_COUNT(get_level_mils(encoder)));
  send_level(encoder, STANDARD_SEND_REPEAT_COUNT);
}

/** @brief Handles user press/release of the encoder's momentary contact switch. */
static void on_sw_change(struct level_encoder *encoder) {
  if (encoder->sw_value) {  // on release
    send_level(encoder, STANDARD_SEND_REPEAT_COUNT);
  }
}

/** @brief Handles receipt of a data payload. */
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

/** @brief Handles firmware entry call. */
void app_main(void) {
  // Read GPIO pins serving as straps.
  initialize_strap(straps, "recv", RECV_STRAP_GPIO);
  ESP_LOGI(tag, "mode=%s", IS_MODE_XMIT ? "xmit" : "recv");

  // Non-volatile storage. Needed by wifi and shared data persistence.
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
    // Watchdogs could go here.
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
