#pragma once
#include <stdint.h>
#define pdPASS 1
#define pdTRUE 1
#define pdFALSE 0
#define pdMS_TO_TICKS(ms) (ms)
#define portMAX_DELAY UINT32_MAX
typedef unsigned TickType_t;
typedef int BaseType_t;
