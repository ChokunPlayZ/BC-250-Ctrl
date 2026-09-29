#include "button_service.h"

#include <string.h>

#include "app_events.h"
#include "core/button_logic.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gpio_service.h"

static const char *TAG = "buttons";
static bc250_config_t s_config;
static bc250_button_logic_t s_runtime[BC250_MAX_BUTTONS];

static uint64_t now_ms(void)
{
    return (uint64_t)(esp_timer_get_time() / 1000);
}

static void emit_action(bc250_button_action_t action)
{
    if (action == BC250_BUTTON_ACTION_NONE) return;
    bc250_app_event_t event = {
        .type = BC250_EVENT_BUTTON_ACTION,
        .data.button_action = action,
    };
    if (!bc250_app_event_post(&event, 0)) {
        ESP_LOGW(TAG, "Button action %s dropped: event queue full", bc250_button_action_name(action));
    }
}

static void process_button(int index, uint64_t now)
{
    const bc250_button_config_t *cfg = &s_config.buttons[index];
    bc250_button_logic_t *rt = &s_runtime[index];
    bool raw = bc250_gpio_read(&cfg->input);
    bool was_pressed = rt->stable;
    bc250_button_gesture_t gesture = bc250_button_logic_update(
        rt, raw, now, cfg->input.debounce_ms, cfg->double_press_ms, cfg->long_press_ms);
    if (rt->stable != was_pressed) {
        ESP_LOGI(TAG, "Button [%d] GPIO %d: %s", index, cfg->input.gpio, rt->stable ? "pressed" : "released");
    }
    const char *gesture_name;
    bc250_button_action_t action;
    if (gesture == BC250_GESTURE_SHORT) { gesture_name = "short press"; action = cfg->short_action; }
    else if (gesture == BC250_GESTURE_DOUBLE) { gesture_name = "double press"; action = cfg->double_action; }
    else if (gesture == BC250_GESTURE_LONG) { gesture_name = "long press"; action = cfg->long_action; }
    else return;
    ESP_LOGI(TAG, "Button [%d] GPIO %d: %s -> %s", index, cfg->input.gpio,
             gesture_name, bc250_button_action_name(action));
    emit_action(action);
}

static void button_task(void *arg)
{
    (void)arg;
    while (true) {
        uint64_t now = now_ms();
        for (int i = 0; i < s_config.button_count; ++i) {
            if (s_config.buttons[i].enabled) process_button(i, now);
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

esp_err_t bc250_button_service_start(const bc250_config_t *config)
{
    if (config == NULL) return ESP_ERR_INVALID_ARG;
    s_config = *config;
    memset(s_runtime, 0, sizeof(s_runtime));
    for (int i = 0; i < s_config.button_count; ++i) {
        if (!s_config.buttons[i].enabled) continue;
        ESP_RETURN_ON_ERROR(bc250_gpio_init_input(&s_config.buttons[i].input), TAG, "button %d", i);
        bc250_button_logic_init(&s_runtime[i], bc250_gpio_read(&s_config.buttons[i].input), now_ms());
        ESP_LOGI(TAG, "Button [%d] GPIO %d ready (%s)", i, s_config.buttons[i].input.gpio,
                 s_runtime[i].stable ? "pressed" : "released");
    }
    return xTaskCreate(button_task, "buttons", 3072, NULL, 5, NULL) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
