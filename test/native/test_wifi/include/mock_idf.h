#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include "esp_err.h"

#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_TIMEOUT 0x107
#define ESP_RETURN_ON_ERROR(call, tag, ...) do { \
    (void)(tag); esp_err_t result = (call); if (result != ESP_OK) return result; \
} while (0)
#define ESP_ERROR_CHECK_WITHOUT_ABORT(call) ((void)(call))
#define ESP_LOGI(tag, ...) ((void)(tag))
#define ESP_LOGW(tag, ...) ((void)(tag))
#define WIFI_EVENT ((esp_event_base_t)1)
#define IP_EVENT ((esp_event_base_t)2)
#define ESP_EVENT_ANY_ID (-1)
#define WIFI_EVENT_STA_DISCONNECTED 1
#define WIFI_EVENT_AP_STACONNECTED 3
#define WIFI_EVENT_AP_STADISCONNECTED 4
#define IP_EVENT_STA_GOT_IP 2
#define IPSTR "%u.%u.%u.%u"
#define IP2STR(ip) 192U, 168U, 1U, (unsigned)(*(ip))
#define WIFI_STORAGE_RAM 0
#define WIFI_MODE_STA 1
#define WIFI_MODE_AP 2
#define WIFI_IF_STA 0
#define WIFI_IF_AP 1
#define WIFI_AUTH_OPEN 0
#define WIFI_AUTH_WPA2_PSK 3
#define WIFI_ALL_CHANNEL_SCAN 0
#define WIFI_CONNECT_AP_BY_SIGNAL 0
#define WIFI_INIT_CONFIG_DEFAULT() ((wifi_init_config_t){0})
#define pdMS_TO_TICKS(ms) (ms)
#define pdPASS 1

typedef const void *esp_event_base_t;
typedef void (*event_handler_t)(void *, esp_event_base_t, int32_t, void *);
typedef void (*TaskFunction_t)(void *);
typedef uint32_t TickType_t;
typedef struct { unsigned num; } wifi_sta_list_t;
typedef struct { int placeholder; } wifi_init_config_t;
typedef struct { int placeholder; } esp_netif_t;
typedef struct { struct { uint32_t ip; } ip_info; } ip_event_got_ip_t;
typedef struct {
    struct {
        uint8_t ssid[32]; unsigned ssid_len; unsigned channel; int authmode;
        unsigned max_connection; struct { bool required; } pmf_cfg;
    } ap;
    struct {
        uint8_t ssid[32]; uint8_t password[64]; int scan_method; int sort_method;
        struct { int authmode; int rssi_5g_adjustment; } threshold;
    } sta;
} wifi_config_t;

esp_err_t esp_netif_init(void);
esp_err_t esp_event_loop_create_default(void);
esp_netif_t *esp_netif_create_default_wifi_ap(void);
esp_netif_t *esp_netif_create_default_wifi_sta(void);
esp_err_t esp_event_handler_register(esp_event_base_t base, int32_t id, event_handler_t handler, void *arg);
esp_err_t esp_wifi_init(const wifi_init_config_t *config);
esp_err_t esp_wifi_set_storage(int storage);
esp_err_t esp_wifi_set_mode(int mode);
esp_err_t esp_wifi_set_config(int interface, const wifi_config_t *config);
esp_err_t esp_wifi_get_mac(int interface, uint8_t *mac);
esp_err_t esp_wifi_start(void);
esp_err_t esp_wifi_stop(void);
esp_err_t esp_wifi_connect(void);
esp_err_t esp_wifi_ap_get_sta_list(wifi_sta_list_t *clients);
TickType_t xTaskGetTickCount(void);
int xTaskCreate(TaskFunction_t function, const char *name, unsigned stack, void *arg, unsigned priority, void *handle);
void vTaskDelay(unsigned ticks);
void vTaskDelete(void *task);

static inline size_t mock_strlcpy(char *dst, const char *src, size_t size)
{
    size_t length = strlen(src);
    if (size) { size_t copied = length < size - 1 ? length : size - 1; memcpy(dst, src, copied); dst[copied] = '\0'; }
    return length;
}
#ifdef strlcpy
#undef strlcpy
#endif
#define strlcpy mock_strlcpy
