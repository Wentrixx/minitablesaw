#include <stdio.h>
#include <stdint.h>
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "motor.h"
#define R_EN GPIO_NUM_32
#define R_PWM GPIO_NUM_33
#define L_EN GPIO_NUM_26
#define L_PWM GPIO_NUM_25
#define PWM_FREQ 20000
#define PWM_MAX 1023
static int motor_percent;
void motor_set_speed(int percent)
{
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;

    gpio_set_level(R_EN, percent > 0);
    gpio_set_level(L_EN, percent > 0);
    uint32_t duty = (percent * PWM_MAX) / 100;

    ESP_ERROR_CHECK(ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty));
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0));

    motor_percent = percent;
    printf("Motor PWM: %d%%\n", percent);
}

void motor_init(void)
{
    gpio_config_t en_config = {
        .pin_bit_mask = (1ULL << R_EN) | (1ULL << L_EN),
        .mode = GPIO_MODE_OUTPUT
    };
    ESP_ERROR_CHECK(gpio_config(&en_config));

    gpio_set_level(R_EN, 0);
    gpio_set_level(L_EN, 0);

    ESP_ERROR_CHECK(ledc_timer_config(&(ledc_timer_config_t) {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_num = LEDC_TIMER_0,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .freq_hz = PWM_FREQ,
        .clk_cfg = LEDC_AUTO_CLK
    }));

    ESP_ERROR_CHECK(ledc_channel_config(&(ledc_channel_config_t) {
        .gpio_num = R_PWM,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
        .hpoint = 0
    }));

    gpio_set_direction(L_PWM, GPIO_MODE_OUTPUT);
    gpio_set_level(L_PWM, 0);

    // Keep the bridge disabled until an explicit nonzero command.
    gpio_set_level(R_EN, 0);
    gpio_set_level(L_EN, 0);
}


void motor_stop(void) { motor_set_speed(0); }
int motor_get_speed(void) { return motor_percent; }

