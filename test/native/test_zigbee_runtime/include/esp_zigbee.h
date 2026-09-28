#pragma once
#include "esp_err.h"

#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_TIMEOUT 0x107
esp_err_t esp_zigbee_launch_mainloop(void);
esp_err_t esp_zigbee_stop(void);
esp_err_t esp_zigbee_task_queue_post(void (*cb)(void *), void *arg);
