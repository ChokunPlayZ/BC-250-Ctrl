#pragma once
#include <stddef.h>
#include <string.h>
static inline void esp_fill_random(void *output, size_t size) { memset(output, 0x42, size); }
