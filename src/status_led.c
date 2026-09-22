#include "status_led.h"

#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gpio_service.h"
#include "power_service.h"

static bc250_output_config_t s_led;
static volatile bool s_config_mode;

static bool led_pattern(bc250_power_state_t state, uint32_t slot)
{
    if (s_config_mode) return slot % 20 == 0 || slot % 20 == 2;
    switch (state) {
    case BC250_POWER_ON: return true;
    case BC250_POWER_STARTING: return (slot % 4) < 2;
    case BC250_POWER_STOPPING: return (slot % 16) < 8;
    case BC250_POWER_FAULT: return slot % 20 == 0 || slot % 20 == 2 || slot % 20 == 4;
    case BC250_POWER_OFF:
    case BC250_POWER_UNKNOWN:
    default: return false;
    }
}

static void led_task(void *arg)
{
    (void)arg;
    uint32_t slot = 0;
    while (true) {
        bc250_gpio_write(&s_led, led_pattern(bc250_power_service_state(), slot++));
        vTaskDelay(pdMS_TO_TICKS(125));
    }
}

esp_err_t bc250_status_led_start(const bc250_config_t *config)
{
    if (config == NULL) return ESP_ERR_INVALID_ARG;
    s_led = config->status_led;
    ESP_RETURN_ON_ERROR(bc250_gpio_init_output(&s_led), "status_led", "LED GPIO");
    bc250_gpio_write(&s_led, false);
    return xTaskCreate(led_task, "status_led", 2048, NULL, 2, NULL) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

void bc250_status_led_set_config_mode(bool active)
{
    s_config_mode = active;
}
