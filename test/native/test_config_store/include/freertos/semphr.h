#pragma once
#include "freertos/FreeRTOS.h"
typedef void *SemaphoreHandle_t;
static inline SemaphoreHandle_t xSemaphoreCreateMutex(void) { return (void *)1; }
static inline int xSemaphoreTake(SemaphoreHandle_t lock, TickType_t wait) { (void)lock; (void)wait; return 1; }
static inline int xSemaphoreGive(SemaphoreHandle_t lock) { (void)lock; return 1; }
