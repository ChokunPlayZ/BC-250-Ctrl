#pragma once
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#define NVS_READWRITE 1
#define ESP_ERR_NVS_NOT_FOUND 0x1102
typedef int nvs_handle_t;
esp_err_t nvs_get_blob(nvs_handle_t, const char *, void *, size_t *);
static inline esp_err_t nvs_open(const char *name, int mode, nvs_handle_t *handle) { (void)name; (void)mode; *handle = 1; return ESP_OK; }
static inline void nvs_close(nvs_handle_t handle) { (void)handle; }
static inline esp_err_t nvs_set_blob(nvs_handle_t handle, const char *key, const void *value, size_t size) { (void)handle; (void)key; (void)value; (void)size; return ESP_OK; }
static inline esp_err_t nvs_get_u8(nvs_handle_t handle, const char *key, uint8_t *value) { (void)handle; (void)key; *value = 0; return ESP_OK; }
static inline esp_err_t nvs_set_u8(nvs_handle_t handle, const char *key, uint8_t value) { (void)handle; (void)key; (void)value; return ESP_OK; }
static inline esp_err_t nvs_commit(nvs_handle_t handle) { (void)handle; return ESP_OK; }
static inline esp_err_t nvs_erase_key(nvs_handle_t handle, const char *key) { (void)handle; (void)key; return ESP_OK; }
static inline esp_err_t nvs_erase_all(nvs_handle_t handle) { (void)handle; return ESP_OK; }
