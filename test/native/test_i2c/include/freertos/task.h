#pragma once
#include "FreeRTOS.h"
BaseType_t xTaskCreate(void (*task)(void *), const char *name, uint32_t stack, void *arg,
                       unsigned priority, void *handle);
void vTaskDelay(TickType_t ticks);
