#include "pwm.h"

#include "driver/ledc.h"

#define LEDC_TIMER LEDC_TIMER_0
#define LEDC_MODE LEDC_LOW_SPEED_MODE
#define LEDC_CHANNEL LEDC_CHANNEL_0
#define LEDC_DUTY_RES LEDC_TIMER_7_BIT
#define LEDC_PWM_FRAME_RATE 1000

/** Set up a singleton timer to drive all PWM outputs. */
void initialize_pwm_timer(void) {
  ledc_timer_config_t timer = {.speed_mode = LEDC_MODE,
                               .timer_num = LEDC_TIMER,
                               .duty_resolution = LEDC_DUTY_RES,
                               .freq_hz = LEDC_PWM_FRAME_RATE,
                               .clk_cfg = LEDC_AUTO_CLK};
  ledc_timer_config(&timer);
}

/** Initialize a GPIO pin as PWM connected to the singleton timer. */
void initialize_pwm(struct pwm *pwm, char *name, gpio_num_t gpio, uint16_t init_duty) {
  pwm->name = name;
  pwm->gpio = gpio;
  pwm->duty = init_duty;
  ledc_channel_config_t config = {.speed_mode = LEDC_MODE,
                                  .channel = LEDC_CHANNEL,
                                  .timer_sel = LEDC_TIMER,
                                  .intr_type = LEDC_INTR_DISABLE,
                                  .gpio_num = gpio,
                                  .duty = init_duty,
                                  .hpoint = 0};
  ledc_channel_config(&config);
}

void set_pwm_duty(struct pwm *pwm, uint16_t duty) {
  ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, duty);
  ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
}
