#include "power_service.h"

#include <string.h>

#include "app_events.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "gpio_service.h"

static const char *TAG = "power";
static bc250_config_t s_config;
static bc250_power_logic_t s_logic;
static QueueHandle_t s_requests;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static bool s_sensed_on;

static uint64_t now_ms(void)
{
    return (uint64_t)(esp_timer_get_time() / 1000);
}

static void apply_outputs(const bc250_power_outputs_t *outputs)
{
    bc250_gpio_write(&s_config.ps_on, outputs->ps_on);
    bc250_gpio_write(&s_config.power_button, outputs->power_button);
}

static bool update_sense(bool filtered, bool raw, uint64_t *changed_at, uint64_t now)
{
    static bool previous_raw;
    if (raw != previous_raw) {
        previous_raw = raw;
        *changed_at = now;
    }
    uint32_t threshold = raw ? s_config.sense_on_ms : s_config.sense_off_ms;
    if (raw != filtered && now - *changed_at >= threshold) return raw;
    return filtered;
}

static void power_task(void *arg)
{
    (void)arg;
    uint64_t raw_changed_at = now_ms();
    bool initial = bc250_gpio_read(&s_config.power_sense);
    s_sensed_on = initial;
    bc250_power_logic_init(&s_logic, &s_config.timing, initial, now_ms());
    apply_outputs(&s_logic.outputs);
    bc250_power_state_t announced = s_logic.state;

    while (true) {
        bc250_power_action_t action;
        while (xQueueReceive(s_requests, &action, 0) == pdTRUE) {
            portENTER_CRITICAL(&s_lock);
            bool accepted = bc250_power_request(&s_logic, action, s_sensed_on, now_ms());
            apply_outputs(&s_logic.outputs);
            portEXIT_CRITICAL(&s_lock);
            ESP_LOGI(TAG, "Power action %d %s", action, accepted ? "accepted" : "rejected");
        }

        uint64_t now = now_ms();
        bool raw = bc250_gpio_read(&s_config.power_sense);
        s_sensed_on = update_sense(s_sensed_on, raw, &raw_changed_at, now);

        portENTER_CRITICAL(&s_lock);
        bc250_power_tick(&s_logic, s_sensed_on, now);
        apply_outputs(&s_logic.outputs);
        bc250_power_state_t state = s_logic.state;
        portEXIT_CRITICAL(&s_lock);

        if (state != announced) {
            announced = state;
            ESP_LOGI(TAG, "Power state: %s", bc250_power_state_name(state));
            bc250_app_event_t event = {
                .type = BC250_EVENT_POWER_STATE_CHANGED,
                .data.power_state = state,
            };
            bc250_app_event_post(&event, 0);
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

esp_err_t bc250_power_service_start(const bc250_config_t *config)
{
    if (config == NULL) return ESP_ERR_INVALID_ARG;
    s_config = *config;
    ESP_RETURN_ON_ERROR(bc250_gpio_init_output(&s_config.ps_on), TAG, "PS_ON GPIO");
    ESP_RETURN_ON_ERROR(bc250_gpio_init_output(&s_config.power_button), TAG, "power button GPIO");
    ESP_RETURN_ON_ERROR(bc250_gpio_init_input(&s_config.power_sense), TAG, "power sense GPIO");
    bc250_gpio_write(&s_config.ps_on, false);
    bc250_gpio_write(&s_config.power_button, false);
    s_requests = xQueueCreate(8, sizeof(bc250_power_action_t));
    if (s_requests == NULL) return ESP_ERR_NO_MEM;
    return xTaskCreate(power_task, "power", 4096, NULL, 8, NULL) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

bool bc250_power_service_request(bc250_power_action_t action)
{
    return s_requests != NULL && xQueueSend(s_requests, &action, 0) == pdTRUE;
}

bc250_power_state_t bc250_power_service_state(void)
{
    portENTER_CRITICAL(&s_lock);
    bc250_power_state_t state = s_logic.state;
    portEXIT_CRITICAL(&s_lock);
    return state;
}

bool bc250_power_service_sensed_on(void)
{
    return s_sensed_on;
}

bc250_power_outputs_t bc250_power_service_outputs(void)
{
    portENTER_CRITICAL(&s_lock);
    bc250_power_outputs_t outputs = s_logic.outputs;
    portEXIT_CRITICAL(&s_lock);
    return outputs;
}
