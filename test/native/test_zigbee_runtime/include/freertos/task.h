#pragma once
#include <stddef.h>
#include "FreeRTOS.h"

typedef void (*TaskFunction_t)(void *);
int xTaskCreate(TaskFunction_t fn, const char *name, unsigned stack, void *arg, unsigned priority, void *handle);
