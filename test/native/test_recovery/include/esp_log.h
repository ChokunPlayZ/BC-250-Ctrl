#pragma once

#include "esp_err.h"

void recovery_test_log(const char *format, ...);
const char *esp_err_to_name(esp_err_t error);
#define ESP_LOGI(tag, ...) do { (void)(tag); recovery_test_log(__VA_ARGS__); } while (0)
#define ESP_LOGW(tag, ...) ESP_LOGI(tag, __VA_ARGS__)
#define ESP_LOGE(tag, ...) ESP_LOGI(tag, __VA_ARGS__)
