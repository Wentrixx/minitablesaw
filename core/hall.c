#include "driver/gpio.h"
#include "hall.h"
#define HALL_PIN GPIO_NUM_27
void hall_init(void)
{
    gpio_config_t hall_config = {
        .pin_bit_mask = (1ULL << HALL_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    ESP_ERROR_CHECK(gpio_config(&hall_config));
}


int hall_get_state(void) { return gpio_get_level(HALL_PIN); }

