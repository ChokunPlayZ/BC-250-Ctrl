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

static const char *action_name(bc250_power_action_t action)
{
    switch (action) {
    case BC250_POWER_ACTION_ON: return "on";
    case BC250_POWER_ACTION_OFF: return "off";
    case BC250_POWER_ACTION_TOGGLE: return "toggle";
    case BC250_POWER_ACTION_FORCE_OFF: return "force_off";
    default: return "none/invalid";
    }
}

static uint64_t now_ms(void)
{
    return (uint64_t)(esp_timer_get_time() / 1000);
}

static void apply_outputs(const bc250_power_outputs_t *outputs)
{
    static bc250_power_outputs_t previous;
    bc250_gpio_write(&s_config.ps_on, outputs->ps_on);
    bc250_gpio_write(&s_config.power_button, outputs->power_button);
    if (outputs->ps_on != previous.ps_on) {
        ESP_LOGI(TAG, "PS_ON output: %s (GPIO %d)", outputs->ps_on ? "active" : "inactive", s_config.ps_on.gpio);
    }
    if (outputs->power_button != previous.power_button) {
        ESP_LOGI(TAG, "Power button output: %s (GPIO %d)", outputs->power_button ? "active" : "inactive",
                 s_config.power_button.gpio);
    }
    previous = *outputs;
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
    uint32_t settle_ms = s_config.sense_on_ms > s_config.sense_off_ms ?
                         s_config.sense_on_ms : s_config.sense_off_ms;
    uint64_t sense_ready_at = raw_changed_at + settle_ms;
    bool initial = bc250_gpio_read(&s_config.power_sense);
    s_sensed_on = initial;
    bc250_power_logic_init(&s_logic, &s_config.timing, initial, now_ms());
    apply_outputs(&s_logic.outputs);
    bc250_power_state_t announced = s_logic.state;
    ESP_LOGI(TAG, "Initial power state: %s; sense: %s; outputs inactive",
             bc250_power_state_name(announced), initial ? "on" : "off");

    while (true) {
        uint64_t now = now_ms();
        bool raw = bc250_gpio_read(&s_config.power_sense);
        bool was_sensed_on = s_sensed_on;
        s_sensed_on = update_sense(s_sensed_on, raw, &raw_changed_at, now);
        if (s_sensed_on != was_sensed_on) {
            ESP_LOGI(TAG, "Power sense changed: %s -> %s", was_sensed_on ? "on" : "off", s_sensed_on ? "on" : "off");
        }

        bc250_power_action_t action;
        while (now >= sense_ready_at && xQueueReceive(s_requests, &action, 0) == pdTRUE) {
            portENTER_CRITICAL(&s_lock);
            bc250_power_state_t before = s_logic.state;
            bool accepted = bc250_power_request(&s_logic, action, s_sensed_on, now);
            bc250_power_outputs_t outputs = s_logic.outputs;
            bc250_power_state_t after = s_logic.state;
            portEXIT_CRITICAL(&s_lock);
            apply_outputs(&outputs);
            bool wants_on = action == BC250_POWER_ACTION_ON ||
                            (action == BC250_POWER_ACTION_TOGGLE && !s_sensed_on);
            if (!accepted) {
                const char *reason = wants_on ? (before == BC250_POWER_STOPPING ? "shutdown in progress" : "retry cooldown active") :
                                               "invalid action";
                ESP_LOGW(TAG, "Power action %s rejected: %s (state=%s; sense=%s)",
                         action_name(action), reason, bc250_power_state_name(before), s_sensed_on ? "on" : "off");
            } else {
                ESP_LOGI(TAG, "Power action %s accepted: %s -> %s", action_name(action),
                         bc250_power_state_name(before), bc250_power_state_name(after));
                if (before != BC250_POWER_STARTING && after == BC250_POWER_STARTING) {
                    ESP_LOGI(TAG, "Power-on sequence triggered");
                } else if (wants_on) {
                    ESP_LOGI(TAG, "Power-on skipped: %s", s_sensed_on ? "power sense already on" : "sequence already starting");
                } else if (after == BC250_POWER_STOPPING && action == BC250_POWER_ACTION_FORCE_OFF && s_sensed_on) {
                    ESP_LOGI(TAG, "Force-off button hold triggered");
                } else if (before != BC250_POWER_STOPPING && after == BC250_POWER_STOPPING) {
                    ESP_LOGI(TAG, "Shutdown sequence triggered");
                }
            }
        }

        portENTER_CRITICAL(&s_lock);
        bc250_power_tick(&s_logic, s_sensed_on, now);
        bc250_power_outputs_t outputs = s_logic.outputs;
        bc250_power_state_t state = s_logic.state;
        portEXIT_CRITICAL(&s_lock);
        apply_outputs(&outputs);

        if (state != announced || s_sensed_on != was_sensed_on) {
            announced = state;
            if (state == BC250_POWER_FAULT) {
                ESP_LOGW(TAG, "Power state: fault; sequence timed out; sense=%s; outputs inactive; retry cooldown applies",
                         s_sensed_on ? "on" : "off");
            } else {
                ESP_LOGI(TAG, "Power state: %s (sense=%s)", bc250_power_state_name(state), s_sensed_on ? "on" : "off");
            }
            bc250_app_event_t event = {
                .type = BC250_EVENT_POWER_STATE_CHANGED,
                .data.power_state = state,
            };
            if (!bc250_app_event_post(&event, 0)) {
                ESP_LOGW(TAG, "Power state notification dropped: event queue full");
            }
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
    bool queued = s_requests != NULL && xQueueSend(s_requests, &action, 0) == pdTRUE;
    if (queued) ESP_LOGI(TAG, "Power action %s queued", action_name(action));
    else ESP_LOGW(TAG, "Power action %s not queued: %s", action_name(action),
                  s_requests == NULL ? "service unavailable" : "request queue full");
    return queued;
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
