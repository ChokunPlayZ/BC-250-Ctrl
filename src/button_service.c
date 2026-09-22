#include "button_service.h"

#include <string.h>

#include "app_events.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gpio_service.h"

typedef struct {
    bool raw;
    bool stable;
    bool long_sent;
    uint8_t clicks;
    uint64_t raw_changed_ms;
    uint64_t pressed_ms;
    uint64_t released_ms;
} button_runtime_t;

static const char *TAG = "buttons";
static bc250_config_t s_config;
static button_runtime_t s_runtime[BC250_MAX_BUTTONS];

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
    if (!bc250_app_event_post(&event, 0)) ESP_LOGW(TAG, "event queue full");
}

static void process_button(int index, uint64_t now)
{
    const bc250_button_config_t *cfg = &s_config.buttons[index];
    button_runtime_t *rt = &s_runtime[index];
    bool raw = bc250_gpio_read(&cfg->input);
    if (raw != rt->raw) {
        rt->raw = raw;
        rt->raw_changed_ms = now;
    }
    if (rt->stable != raw && now - rt->raw_changed_ms >= cfg->input.debounce_ms) {
        rt->stable = raw;
        if (raw) {
            rt->pressed_ms = now;
            rt->long_sent = false;
        } else if (!rt->long_sent) {
            rt->clicks++;
            rt->released_ms = now;
        }
    }
    if (rt->stable && !rt->long_sent && now - rt->pressed_ms >= cfg->long_press_ms) {
        rt->long_sent = true;
        rt->clicks = 0;
        emit_action(cfg->long_action);
    }
    if (!rt->stable && rt->clicks > 0 && now - rt->released_ms >= cfg->double_press_ms) {
        emit_action(rt->clicks >= 2 ? cfg->double_action : cfg->short_action);
        rt->clicks = 0;
    }
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
        s_runtime[i].raw = bc250_gpio_read(&s_config.buttons[i].input);
        s_runtime[i].stable = s_runtime[i].raw;
    }
    return xTaskCreate(button_task, "buttons", 3072, NULL, 5, NULL) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
