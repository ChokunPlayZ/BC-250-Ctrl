#include "wifi_service.h"

#include <string.h>

#include "esp_event.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include "status_led.h"
#include "web_server.h"

static const char *TAG = "wifi";
#define CONFIG_AP_IDLE_MS (5U * 60U * 1000U)
static bc250_config_t s_config;
static esp_netif_t *s_ap_netif;
static esp_netif_t *s_sta_netif;
static bool s_wifi_initialized;
static bool s_wifi_started;
static bool s_dns_started;
static volatile bool s_config_ap;
static bool s_connected;
static volatile uint32_t s_ap_generation;
static volatile TickType_t s_ap_last_activity;
static char s_ip[16] = "0.0.0.0";
static esp_err_t start_station(void);

static void wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT && s_config_ap &&
        (id == WIFI_EVENT_AP_STACONNECTED || id == WIFI_EVENT_AP_STADISCONNECTED)) {
        /* A full five-minute grace period starts when the last client leaves. */
        s_ap_last_activity = xTaskGetTickCount();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED && !s_config_ap) {
        s_connected = false;
        strlcpy(s_ip, "0.0.0.0", sizeof(s_ip));
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP && !s_config_ap) {
        const ip_event_got_ip_t *event = data;
        snprintf(s_ip, sizeof(s_ip), IPSTR, IP2STR(&event->ip_info.ip));
        s_connected = true;
        ESP_LOGI(TAG, "Station connected at %s", s_ip);
    }
}

static esp_err_t init_wifi_once(void)
{
    if (s_wifi_initialized) return ESP_OK;
    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "netif init");
    esp_err_t event_err = esp_event_loop_create_default();
    if (event_err != ESP_OK && event_err != ESP_ERR_INVALID_STATE) return event_err;
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&init), TAG, "Wi-Fi init");
    ESP_RETURN_ON_ERROR(esp_wifi_set_storage(WIFI_STORAGE_RAM), TAG, "Wi-Fi storage");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event, NULL), TAG, "Wi-Fi events");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event, NULL), TAG, "IP events");
    s_wifi_initialized = true;
    return ESP_OK;
}

static void dns_task(void *arg)
{
    (void)arg;
    /* One worker owns the socket, including across rapid AP close/reopen cycles. */
    while (true) {
        if (!s_config_ap) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        uint32_t generation = s_ap_generation;
        int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
        if (sock < 0) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        struct sockaddr_in address = {
            .sin_family = AF_INET,
            .sin_port = htons(53),
            .sin_addr.s_addr = htonl(INADDR_ANY),
        };
        if (bind(sock, (struct sockaddr *)&address, sizeof(address)) != 0) {
            close(sock);
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        struct timeval receive_timeout = {.tv_sec = 1};
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &receive_timeout, sizeof(receive_timeout));
        uint8_t request[256];
        while (s_config_ap && generation == s_ap_generation) {
            struct sockaddr_in source;
            socklen_t source_len = sizeof(source);
            int length = recvfrom(sock, request, sizeof(request), 0,
                                  (struct sockaddr *)&source, &source_len);
            if (!s_config_ap || generation != s_ap_generation) break;
            if (length < 12) continue;
            uint8_t response[300];
            if ((size_t)length + 16 > sizeof(response)) continue;
            memcpy(response, request, length);
            response[2] = 0x81;
            response[3] = 0x80;
            response[6] = 0;
            response[7] = 1;
            size_t pos = length;
            const uint8_t answer[] = {0xc0, 0x0c, 0x00, 0x01, 0x00, 0x01,
                                      0x00, 0x00, 0x00, 0x1e, 0x00, 0x04,
                                      192, 168, 4, 1};
            memcpy(&response[pos], answer, sizeof(answer));
            pos += sizeof(answer);
            sendto(sock, response, pos, 0, (struct sockaddr *)&source, source_len);
        }
        close(sock);
    }
}

static void ap_expiry_task(void *arg)
{
    uint32_t generation = (uint32_t)(uintptr_t)arg;
    while (s_config_ap && generation == s_ap_generation) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        if (!s_config_ap || generation != s_ap_generation) break;
        wifi_sta_list_t clients = {0};
        /* Never disconnect clients, or assume an unreadable client list is empty. */
        if (esp_wifi_ap_get_sta_list(&clients) != ESP_OK) continue;
        TickType_t now = xTaskGetTickCount();
        if (clients.num > 0) {
            s_ap_last_activity = now;
        } else if ((TickType_t)(now - s_ap_last_activity) >= pdMS_TO_TICKS(CONFIG_AP_IDLE_MS)) {
            esp_err_t err = bc250_wifi_close_config_ap();
            if (err == ESP_OK) ESP_LOGI(TAG, "Configuration AP closed after five minutes without a client");
            else ESP_LOGW(TAG, "Unable to close idle AP: %s", esp_err_to_name(err));
        }
    }
    vTaskDelete(NULL);
}

static esp_err_t start_ap(void)
{
    ESP_RETURN_ON_ERROR(init_wifi_once(), TAG, "Wi-Fi init");
    if (s_ap_netif == NULL) s_ap_netif = esp_netif_create_default_wifi_ap();
    if (s_ap_netif == NULL) return ESP_ERR_NO_MEM;
    bool already_open = s_config_ap;
    /* Recovery is AP-only so station-side clients cannot bypass portal auth. */
    /* Suppress station reconnects during the mode transition. */
    s_config_ap = true;
    esp_err_t err = esp_wifi_set_mode(WIFI_MODE_AP);
    if (err != ESP_OK) {
        s_config_ap = already_open;
        return err;
    }
    s_connected = false;

    uint8_t mac[6];
    esp_wifi_get_mac(WIFI_IF_AP, mac);
    wifi_config_t ap = {0};
    snprintf((char *)ap.ap.ssid, sizeof(ap.ap.ssid), "BC250-Ctrl-%02X%02X", mac[4], mac[5]);
    ap.ap.ssid_len = strlen((char *)ap.ap.ssid);
    ap.ap.channel = 1;
    ap.ap.authmode = WIFI_AUTH_OPEN;
    ap.ap.max_connection = 4;
    ap.ap.pmf_cfg.required = false;
    err = esp_wifi_set_config(WIFI_IF_AP, &ap);
    if (err != ESP_OK) goto ap_failed;
    if (!s_wifi_started) {
        err = esp_wifi_start();
        if (err != ESP_OK) goto ap_failed;
        s_wifi_started = true;
    }
    ++s_ap_generation;
    s_ap_last_activity = xTaskGetTickCount();
    strlcpy(s_ip, "192.168.4.1", sizeof(s_ip));
    bc250_status_led_set_config_mode(true);
    if (!s_dns_started) {
        if (xTaskCreate(dns_task, "captive_dns", 3072, NULL, 3, NULL) != pdPASS) {
            ESP_ERROR_CHECK_WITHOUT_ABORT(bc250_wifi_close_config_ap());
            return ESP_ERR_NO_MEM;
        }
        s_dns_started = true;
    }
    if (xTaskCreate(ap_expiry_task, "ap_expiry", 3072,
                    (void *)(uintptr_t)s_ap_generation, 2, NULL) != pdPASS) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(bc250_wifi_close_config_ap());
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "Open configuration AP %s started at 192.168.4.1", ap.ap.ssid);
    return bc250_web_server_start();

ap_failed:
    s_config_ap = already_open;
    return err;
}

static esp_err_t start_station(void)
{
    ESP_RETURN_ON_ERROR(init_wifi_once(), TAG, "Wi-Fi init");
    if (s_sta_netif == NULL) s_sta_netif = esp_netif_create_default_wifi_sta();
    if (s_sta_netif == NULL) return ESP_ERR_NO_MEM;
    wifi_config_t sta = {0};
    strlcpy((char *)sta.sta.ssid, s_config.wifi_ssid, sizeof(sta.sta.ssid));
    strlcpy((char *)sta.sta.password, s_config.wifi_password, sizeof(sta.sta.password));
    sta.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    sta.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    sta.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
#if CONFIG_IDF_TARGET_ESP32C5
    sta.sta.threshold.rssi_5g_adjustment = 10;
#endif
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "station mode");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &sta), TAG, "station config");
    if (!s_wifi_started) {
        ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "station start");
        s_wifi_started = true;
    }
    ESP_RETURN_ON_ERROR(esp_wifi_connect(), TAG, "station connect");
    return bc250_web_server_start();
}

esp_err_t bc250_wifi_service_start(const bc250_config_t *config, bool force_config_ap)
{
    if (config == NULL) return ESP_ERR_INVALID_ARG;
    s_config = *config;
    bool wants_station = config->radio_profile == BC250_RADIO_WIFI ||
                         config->radio_profile == BC250_RADIO_HYBRID;
    if (force_config_ap || !config->configured || (wants_station && config->wifi_ssid[0] == '\0')) {
        return start_ap();
    }
    if (wants_station) return start_station();
    return ESP_OK;
}

esp_err_t bc250_wifi_open_config_ap(void)
{
    if (s_config_ap) return ESP_OK;
    return start_ap();
}

esp_err_t bc250_wifi_open_setup_ap(void)
{
    if (s_config_ap) {
        s_ap_last_activity = xTaskGetTickCount();
        return bc250_web_server_start();
    }
    return start_ap();
}

esp_err_t bc250_wifi_close_config_ap(void)
{
    if (!s_config_ap) return ESP_OK;
    /* Keep the AP indicator active if the driver cannot stop. */
    ESP_RETURN_ON_ERROR(esp_wifi_stop(), TAG, "stop configuration AP");
    s_wifi_started = false;
    s_config_ap = false;
    s_connected = false;
    bc250_status_led_set_config_mode(false);
    strlcpy(s_ip, "0.0.0.0", sizeof(s_ip));
    bool wants_station = s_config.configured && s_config.wifi_ssid[0] != '\0' &&
                         (s_config.radio_profile == BC250_RADIO_WIFI ||
                          s_config.radio_profile == BC250_RADIO_HYBRID);
    return wants_station ? start_station() : ESP_OK;
}

bool bc250_wifi_is_config_ap(void)
{
    return s_config_ap;
}

bool bc250_wifi_is_connected(void)
{
    return s_connected;
}

const char *bc250_wifi_ip_address(void)
{
    return s_ip;
}
