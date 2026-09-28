#include "zigbee_runtime.h"

#include "esp_zigbee.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define RUNTIME_RUN (1U << 0)
#define RUNTIME_IDLE (1U << 1)
#define RUNTIME_READY (1U << 2)
#define RUNTIME_STOP_FAILED (1U << 3)
#define RUNTIME_TIMEOUT pdMS_TO_TICKS(5000)

static SemaphoreHandle_t s_lock;
static EventGroupHandle_t s_state;
static bool s_paused;
static void (*s_setup)(void);
static void (*s_teardown)(void);
static esp_err_t s_stop_result;

static void runtime_task(void *arg)
{
    (void)arg;
    while (true) {
        xEventGroupWaitBits(s_state, RUNTIME_RUN, pdTRUE, pdTRUE, portMAX_DELAY);
        xSemaphoreTake(s_lock, portMAX_DELAY);
        if (s_paused) {
            xSemaphoreGive(s_lock);
            continue;
        }
        /* Claim the radio while holding the same lock used by pause. */
        xEventGroupClearBits(s_state, RUNTIME_IDLE | RUNTIME_STOP_FAILED);
        xSemaphoreGive(s_lock);
        s_setup();
        xEventGroupSetBits(s_state, RUNTIME_READY);
        esp_zigbee_launch_mainloop();
        xEventGroupClearBits(s_state, RUNTIME_READY);
        s_teardown();
        xEventGroupSetBits(s_state, RUNTIME_IDLE);
    }
}

static void stop_cb(void *arg)
{
    (void)arg;
    /* SDK calls run on the Zigbee worker, including while it is commissioning. */
    s_stop_result = esp_zigbee_stop();
    if (s_stop_result != ESP_OK) xEventGroupSetBits(s_state, RUNTIME_STOP_FAILED);
}

esp_err_t bc250_zigbee_runtime_start(void (*setup)(void), void (*teardown)(void))
{
    if (setup == NULL || teardown == NULL) return ESP_ERR_INVALID_ARG;
    if (s_state != NULL) return ESP_ERR_INVALID_STATE;
    s_lock = xSemaphoreCreateMutex();
    if (s_lock == NULL) return ESP_ERR_NO_MEM;
    s_state = xEventGroupCreate();
    if (s_state == NULL) {
        vSemaphoreDelete(s_lock);
        s_lock = NULL;
        return ESP_ERR_NO_MEM;
    }
    s_setup = setup;
    s_teardown = teardown;
    xEventGroupSetBits(s_state, RUNTIME_IDLE);
    if (xTaskCreate(runtime_task, "Zigbee_main", 6144, NULL, 5, NULL) != pdPASS) {
        vEventGroupDelete(s_state);
        vSemaphoreDelete(s_lock);
        s_state = NULL;
        s_lock = NULL;
        return ESP_ERR_NO_MEM;
    }
    if (!s_paused) xEventGroupSetBits(s_state, RUNTIME_RUN);
    return ESP_OK;
}

esp_err_t bc250_zigbee_runtime_set_paused(bool paused)
{
    /* Boot may open the recovery AP before the Zigbee service is initialized. */
    if (s_state == NULL) {
        s_paused = paused;
        return ESP_OK;
    }
    if (xSemaphoreTake(s_lock, RUNTIME_TIMEOUT) != pdTRUE) return ESP_ERR_TIMEOUT;
    if (!paused) {
        if (s_paused) {
            s_paused = false;
            xEventGroupSetBits(s_state, RUNTIME_RUN);
        }
        xSemaphoreGive(s_lock);
        return ESP_OK;
    }
    s_paused = true;
    xEventGroupClearBits(s_state, RUNTIME_RUN | RUNTIME_STOP_FAILED);
    /* A task still initializing must reach its mainloop before accepting stop. */
    EventBits_t state = xEventGroupWaitBits(s_state, RUNTIME_READY | RUNTIME_IDLE,
                                          pdFALSE, pdFALSE, RUNTIME_TIMEOUT);
    esp_err_t err = ESP_OK;
    if (!(state & RUNTIME_IDLE)) {
        err = state & RUNTIME_READY ? esp_zigbee_task_queue_post(stop_cb, NULL) : ESP_ERR_TIMEOUT;
        if (err == ESP_OK) {
            state = xEventGroupWaitBits(s_state, RUNTIME_IDLE | RUNTIME_STOP_FAILED,
                                       pdFALSE, pdFALSE, RUNTIME_TIMEOUT);
            err = state & RUNTIME_STOP_FAILED ? s_stop_result :
                  state & RUNTIME_IDLE ? ESP_OK : ESP_ERR_TIMEOUT;
        }
    }
    if (err != ESP_OK) {
        /* A failed AP transition must not leave Zigbee permanently suspended. */
        s_paused = false;
        xEventGroupSetBits(s_state, RUNTIME_RUN);
    }
    xSemaphoreGive(s_lock);
    return err;
}
