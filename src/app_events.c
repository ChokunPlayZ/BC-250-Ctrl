#include "app_events.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

static QueueHandle_t s_queue;

esp_err_t bc250_app_events_init(void)
{
    if (s_queue != NULL) return ESP_OK;
    s_queue = xQueueCreate(24, sizeof(bc250_app_event_t));
    return s_queue != NULL ? ESP_OK : ESP_ERR_NO_MEM;
}

bool bc250_app_event_post(const bc250_app_event_t *event, uint32_t timeout_ms)
{
    return s_queue != NULL && event != NULL &&
           xQueueSend(s_queue, event, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

bool bc250_app_event_receive(bc250_app_event_t *event, uint32_t timeout_ms)
{
    return s_queue != NULL && event != NULL &&
           xQueueReceive(s_queue, event, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

