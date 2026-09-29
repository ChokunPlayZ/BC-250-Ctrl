#pragma once
void mock_log(const char *tag, const char *format, ...);
#define ESP_LOGW(tag, ...) mock_log(tag, __VA_ARGS__)
#define ESP_LOGI(tag, ...) mock_log(tag, __VA_ARGS__)
