/*
 * Design notes:
 *
 * 0-10 volt wireless dimmer. One ESP-32 serves as master control and transmitter.
 * Arbitrary number of other EPS-32s are at each light. Each generates the 0-10 volt
 * dimming control signal. Lights are HYPERLITE High Bay 150W. Their 0-10 volt
 * control input is normally pushed to a floating differential 15v. The control signal
 * source therefore is a sink rather than a source. Observed sink current is 0.1ma. This
 * firmware generates a 3.3 volt PWM output with 0-100% duty cycle. We use the GPIO
 * to drive a lo-pass filter and 3x op-amp stage (12-volt supply) for nominal 0-10v.
 * The LM358P can sinks 5ma at all temps. A buck converter 12 -> 3.3v powers the
 * receiver ESP-32s. The transmitter needs only 3.3v or 5v e.g. from USB.
 */
#include "esp_log.h"
#include "led.h"
#include "level.h"
#include "pwm.h"
#include "wifi.h"

#define LEVEL_SW_GPIO 21
#define LEVEL_CLK_GPIO 22
#define LEVEL_DT_GPIO 23
#define CTRL_OUT_GPIO 25  // PWM
#define LEVEL_MIN 0
#define LEVEL_MAX 64
#define LEVEL_INIT 32

static const char tag[] = "dimmer";
static struct pwm pwm[1];

static void on_level_change(struct level_encoder *encoder) {
  ESP_LOGI(tag, "level=%u", encoder->level);
  struct led *led = encoder->data;
  set_led_flash_count(led, (7 + encoder->level) / 8);
}

static void on_sw_change(struct level_encoder *encoder) {
  ESP_LOGI(tag, "sw=%u", encoder->sw_value);
}

static void on_receive(void *data) {}

static void send_level(uint16_t level) {}

void app_main(void) {
  uint8_t mac[6];
  char buf[40];
  get_mac(mac, buf);
  ESP_LOGI(tag, "mac=%s", buf);
  configure_wifi(on_receive);

  struct led led[1];
  initialize_led(led, "on-board", ONBOARD_LED_GPIO);

  struct level_encoder encoder[1];
  initialize_level_encoder(encoder, "level", LEVEL_SW_GPIO, LEVEL_CLK_GPIO, LEVEL_DT_GPIO,
                           LEVEL_MIN, LEVEL_MAX, LEVEL_INIT, on_level_change, on_sw_change, led);
  on_level_change(encoder);  // Consider initial level a change.
  start_level_encoder_sense(encoder);

  initialize_pwm_timer();
  initialize_pwm(pwm, "ctrl", CTRL_OUT_GPIO, PERCENT_TO_DUTY(50));
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
