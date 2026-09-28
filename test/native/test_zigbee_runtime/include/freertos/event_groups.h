#pragma once
#include "FreeRTOS.h"

typedef uint32_t EventBits_t;
typedef struct mock_event_group *EventGroupHandle_t;
EventGroupHandle_t xEventGroupCreate(void);
EventBits_t xEventGroupSetBits(EventGroupHandle_t group, EventBits_t bits);
EventBits_t xEventGroupClearBits(EventGroupHandle_t group, EventBits_t bits);
EventBits_t xEventGroupWaitBits(EventGroupHandle_t group, EventBits_t bits,
                              int clear, int all, TickType_t timeout);
void vEventGroupDelete(EventGroupHandle_t group);
