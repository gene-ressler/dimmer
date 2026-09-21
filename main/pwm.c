#include "pwm.h"

#include "driver/ledc.h"

#define LEDC_TIMER LEDC_TIMER_0
#define LEDC_MODE LEDC_LOW_SPEED_MODE
#define LEDC_CHANNEL LEDC_CHANNEL_0
#define LEDC_DUTY_RES LEDC_TIMER_7_BIT
#define LEDC_PWM_FRAME_RATE 1000
#define MILS_TO_DUTY(M) ((M) / 8)
#define DUTY_TO_MILS(D) ((D) * 8)

void initialize_pwm_timer(void) {
  ledc_timer_config_t timer = {.speed_mode = LEDC_MODE,
                               .timer_num = LEDC_TIMER,
                               .duty_resolution = LEDC_DUTY_RES,
                               .freq_hz = LEDC_PWM_FRAME_RATE,
                               .clk_cfg = LEDC_AUTO_CLK};
  ledc_timer_config(&timer);
}

void initialize_pwm(struct pwm *pwm, char *name, gpio_num_t gpio, uint16_t init_duty_mils) {
  pwm->name = name;
  pwm->gpio = gpio;
  pwm->duty = MILS_TO_DUTY(init_duty_mils);
  ledc_channel_config_t config = {.speed_mode = LEDC_MODE,
                                  .channel = LEDC_CHANNEL,
                                  .timer_sel = LEDC_TIMER,
                                  .intr_type = LEDC_INTR_DISABLE,
                                  .gpio_num = gpio,
                                  .duty = pwm->duty,
                                  .hpoint = 0};
  ledc_channel_config(&config);
}

void set_pwm_duty_mils(struct pwm *pwm, uint16_t duty_mils) {
  uint16_t old_duty = atomic_load(&pwm->duty);
  uint16_t new_duty = MILS_TO_DUTY(duty_mils);
  if (new_duty == old_duty) {
    return;
  }
  atomic_store(&pwm->duty, new_duty);
  ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, new_duty);
  ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
}

uint16_t get_pwm_duty_mils(struct pwm *pwm) { return DUTY_TO_MILS(atomic_load(&pwm->duty)); }
