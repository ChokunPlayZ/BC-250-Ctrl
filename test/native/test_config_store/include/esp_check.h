#pragma once
#define ESP_ERR_INVALID_CRC 0x109
#define ESP_ERR_NO_MEM 0x101
#define ESP_RETURN_ON_ERROR(call, tag, ...) do { esp_err_t result = (call); if (result != ESP_OK) return result; } while (0)
static inline const char *esp_err_to_name(esp_err_t err) { (void)err; return "mock error"; }
