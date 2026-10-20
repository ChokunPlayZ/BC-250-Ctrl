#pragma once
#include "FreeRTOS.h"
typedef void *TaskHandle_t;
typedef enum { eSetBits } eNotifyAction;
BaseType_t xTaskCreate(void (*task)(void *), const char *, unsigned, void *, unsigned, TaskHandle_t *);
BaseType_t xTaskNotify(TaskHandle_t, uint32_t, eNotifyAction);
BaseType_t xTaskNotifyWait(uint32_t, uint32_t, uint32_t *, TickType_t);
void vTaskDelay(TickType_t);
