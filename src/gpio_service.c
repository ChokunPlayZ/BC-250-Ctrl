#include "gpio_service.h"

#include "driver/gpio.h"

esp_err_t bc250_gpio_init_output(const bc250_output_config_t *config)
{
    if (config == NULL || config->gpio < 0) return ESP_OK;
    gpio_set_level(config->gpio, config->active_high ? 0 : 1);
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << config->gpio,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    return gpio_config(&io);
}

esp_err_t bc250_gpio_init_input(const bc250_input_config_t *config)
{
    if (config == NULL || config->gpio < 0) return ESP_OK;
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << config->gpio,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = config->pull_up ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
        .pull_down_en = config->pull_up ? GPIO_PULLDOWN_DISABLE : GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    return gpio_config(&io);
}

void bc250_gpio_write(const bc250_output_config_t *config, bool active)
{
    if (config == NULL || config->gpio < 0) return;
    gpio_set_level(config->gpio, active == config->active_high ? 1 : 0);
}

bool bc250_gpio_read(const bc250_input_config_t *config)
{
    if (config == NULL || config->gpio < 0) return false;
    return (gpio_get_level(config->gpio) != 0) == config->active_high;
}

