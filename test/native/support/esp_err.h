#pragma once

/* The settings editor needs only error codes; no ESP-IDF runtime on the host. */
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_SIZE 0x104
#define ESP_ERR_INVALID_VERSION 0x10a
