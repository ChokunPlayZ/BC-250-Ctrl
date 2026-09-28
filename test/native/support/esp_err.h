#pragma once

/* The settings editor needs only error codes; no ESP-IDF runtime on the host. */
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_INVALID_ARG 0x102
