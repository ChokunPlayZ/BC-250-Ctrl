#include <assert.h>
#include "mock_idf.h"
#include "wifi_service.h"
#include "zigbee_service.h"

static int mode, connect_calls, web_calls, dns_tasks, expiry_tasks;
static bool driver_started, fail_start, fail_task, led_config_mode, query_error;
static bool zigbee_paused, fail_pause, fail_mode, fail_ap_config;
static int pause_calls, resume_calls;
static unsigned clients, stop_failures, delays;
static TickType_t ticks, elapsed;
static event_handler_t wifi_handler;
static TaskFunction_t expire;
static void *expire_arg;
static wifi_config_t ap_config, sta_config;
static const char *scenario;

esp_err_t esp_netif_init(void) { return ESP_OK; }
esp_err_t esp_event_loop_create_default(void) { return ESP_OK; }
esp_netif_t *esp_netif_create_default_wifi_ap(void) { static esp_netif_t netif; return &netif; }
esp_netif_t *esp_netif_create_default_wifi_sta(void) { static esp_netif_t netif; return &netif; }
esp_err_t esp_wifi_init(const wifi_init_config_t *config) { (void)config; return ESP_OK; }
esp_err_t esp_wifi_set_storage(int storage) { (void)storage; return ESP_OK; }
esp_err_t esp_event_handler_register(esp_event_base_t base, int32_t id, event_handler_t handler, void *arg)
{
    (void)id; (void)arg;
    if (base == WIFI_EVENT) wifi_handler = handler;
    return ESP_OK;
}
esp_err_t esp_wifi_set_mode(int next)
{
    if (next == WIFI_MODE_AP) {
        assert(zigbee_paused);
        if (fail_mode) { fail_mode = false; return ESP_ERR_INVALID_STATE; }
    }
    if (mode == WIFI_MODE_STA && next == WIFI_MODE_AP) {
        wifi_handler(NULL, WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, NULL);
    }
    mode = next;
    return ESP_OK;
}
esp_err_t esp_wifi_set_config(int interface, const wifi_config_t *config)
{
    if (interface == WIFI_IF_AP && fail_ap_config) {
        fail_ap_config = false; return ESP_ERR_INVALID_STATE;
    }
    if (interface == WIFI_IF_AP) ap_config = *config;
    else sta_config = *config;
    return ESP_OK;
}
esp_err_t esp_wifi_get_mac(int interface, uint8_t *mac)
{
    (void)interface; memcpy(mac, (uint8_t[]){0, 1, 2, 3, 0xab, 0xcd}, 6); return ESP_OK;
}
esp_err_t esp_wifi_start(void)
{
    if (mode == WIFI_MODE_AP) assert(zigbee_paused);
    if (fail_start) { fail_start = false; return ESP_ERR_INVALID_STATE; }
    assert(!driver_started); driver_started = true; return ESP_OK;
}
esp_err_t esp_wifi_stop(void)
{
    if (stop_failures) { --stop_failures; return ESP_ERR_INVALID_STATE; }
    driver_started = false; return ESP_OK;
}
esp_err_t esp_wifi_connect(void) { ++connect_calls; return ESP_OK; }
esp_err_t esp_wifi_ap_get_sta_list(wifi_sta_list_t *list)
{
    if (query_error) return ESP_ERR_INVALID_STATE;
    list->num = clients;
    return ESP_OK;
}
esp_err_t bc250_web_server_start(void) { ++web_calls; return ESP_OK; }
esp_err_t bc250_zigbee_set_config_ap_active(bool active)
{
    if (active) {
        ++pause_calls;
        if (fail_pause) { fail_pause = false; return ESP_ERR_INVALID_STATE; }
    } else {
        ++resume_calls;
        assert(!driver_started || mode != WIFI_MODE_AP);
    }
    zigbee_paused = active;
    return ESP_OK;
}
void bc250_status_led_set_config_mode(bool active) { led_config_mode = active; }
TickType_t xTaskGetTickCount(void) { return ticks; }
int xTaskCreate(TaskFunction_t function, const char *name, unsigned stack, void *arg, unsigned priority, void *handle)
{
    (void)stack; (void)priority; (void)handle;
    if (!strcmp(name, "ap_expiry")) {
        if (fail_task) { fail_task = false; return 0; }
        ++expiry_tasks; expire = function; expire_arg = arg;
    }
    if (!strcmp(name, "captive_dns")) ++dns_tasks;
    return pdPASS;
}
static void client_event(unsigned count, int event)
{
    clients = count;
    wifi_handler(NULL, WIFI_EVENT, event, NULL);
}
void vTaskDelay(unsigned duration)
{
    assert(duration == 1000);
    assert(++delays <= 2000); /* Connected scenarios must still eventually finish. */
    ticks += duration; elapsed += duration;
    assert(bc250_wifi_is_config_ap() && led_config_mode);
    if (!strcmp(scenario, "connected") && elapsed == 1200000) {
        client_event(0, WIFI_EVENT_AP_STADISCONNECTED);
    } else if (!strcmp(scenario, "multiple_clients")) {
        if (elapsed == 1200000) client_event(1, WIFI_EVENT_AP_STADISCONNECTED);
        if (elapsed == 1500000) client_event(0, WIFI_EVENT_AP_STADISCONNECTED);
    } else if (!strcmp(scenario, "reconnect")) {
        if (elapsed == 299000) client_event(1, WIFI_EVENT_AP_STACONNECTED);
        if (elapsed == 300000) client_event(0, WIFI_EVENT_AP_STADISCONNECTED);
    } else if (!strcmp(scenario, "query_error") && elapsed == 360000) {
        query_error = false;
    } else if (!strcmp(scenario, "repeat") && elapsed == 299000) {
        assert(bc250_wifi_open_setup_ap() == ESP_OK);
        assert(expiry_tasks == 1 && dns_tasks == 1);
    }
}
void vTaskDelete(void *task) { (void)task; }

int main(int argc, char **argv)
{
    assert(argc == 2); scenario = argv[1];
    bool zigbee = strstr(scenario, "zigbee") != NULL || !strcmp(scenario, "retry") ||
                  !strcmp(scenario, "task_failure") || !strcmp(scenario, "stale") ||
                  !strcmp(scenario, "pause_failure") || !strcmp(scenario, "mode_failure") ||
                  !strcmp(scenario, "config_failure");
    bool first = !strcmp(scenario, "first_boot");
    bool recovery = strstr(scenario, "recovery") || strstr(scenario, "expiry");
    bc250_config_t config = {.configured = !first, .radio_profile = zigbee ? BC250_RADIO_ZIGBEE :
                            BC250_RADIO_WIFI};
    strcpy(config.wifi_ssid, "test-network"); strcpy(config.wifi_password, "test-password");
    if (!strcmp(scenario, "wrap")) ticks = UINT32_MAX - 150000;
    assert(bc250_wifi_service_start(&config, recovery) == ESP_OK);
    int previous_connects = connect_calls;
    if (!strcmp(scenario, "pause_failure") || !strcmp(scenario, "mode_failure") ||
        !strcmp(scenario, "config_failure") || !strcmp(scenario, "config_failure_wifi")) {
        fail_pause = !strcmp(scenario, "pause_failure");
        fail_mode = !strcmp(scenario, "mode_failure");
        fail_ap_config = !strncmp(scenario, "config_failure", 14);
        assert(bc250_wifi_open_setup_ap() != ESP_OK);
        assert(!bc250_wifi_is_config_ap() && !led_config_mode && !zigbee_paused);
        assert(driver_started == !zigbee);
        previous_connects = connect_calls;
    }
    if (!strcmp(scenario, "retry")) {
        fail_start = true;
        assert(bc250_wifi_open_setup_ap() != ESP_OK);
        assert(!bc250_wifi_is_config_ap() && !led_config_mode && !zigbee_paused);
    }
    if (!strcmp(scenario, "task_failure")) {
        fail_task = true;
        assert(bc250_wifi_open_setup_ap() == ESP_ERR_NO_MEM);
        assert(!bc250_wifi_is_config_ap() && !led_config_mode && !driver_started && !zigbee_paused);
    }
    assert(bc250_wifi_open_setup_ap() == ESP_OK);
    assert(bc250_wifi_is_config_ap() && driver_started && mode == WIFI_MODE_AP && led_config_mode);
    assert(zigbee_paused && pause_calls > 0);
    assert(!strcmp(bc250_wifi_ip_address(), "192.168.4.1"));
    assert(!strcmp((char *)ap_config.ap.ssid, "BC250-Ctrl-ABCD"));
    assert(ap_config.ap.authmode == WIFI_AUTH_OPEN && ap_config.ap.max_connection == 4);
    assert(connect_calls == previous_connects); /* No reconnect during the AP mode transition. */
    assert(expiry_tasks == 1);
    int previous_web_calls = web_calls;
    assert(bc250_wifi_open_setup_ap() == ESP_OK);
    assert(web_calls == previous_web_calls + 1 && expiry_tasks == 1);
    int previous_pauses = pause_calls;
    assert(bc250_wifi_open_config_ap() == ESP_OK && pause_calls == previous_pauses);
    if (!strcmp(scenario, "stale")) {
        TaskFunction_t old_expire = expire; void *old_arg = expire_arg;
        assert(bc250_wifi_close_config_ap() == ESP_OK);
        assert(bc250_wifi_open_config_ap() == ESP_OK);
        old_expire(old_arg);
        assert(bc250_wifi_is_config_ap() && led_config_mode && delays == 0);
    }
    if (!strcmp(scenario, "manual_close")) {
        assert(bc250_wifi_close_config_ap() == ESP_OK);
        expire(expire_arg);
        assert(delays == 0);
        assert(bc250_wifi_close_config_ap() == ESP_OK); /* Idempotent close. */
    } else {
        if (!strcmp(scenario, "connected")) client_event(1, WIFI_EVENT_AP_STACONNECTED);
        if (!strcmp(scenario, "multiple_clients")) client_event(2, WIFI_EVENT_AP_STACONNECTED);
        query_error = !strcmp(scenario, "query_error");
        if (!strcmp(scenario, "stop_failure")) stop_failures = 2;
        expire(expire_arg);
        TickType_t expected = !strcmp(scenario, "connected") ? 1500000 :
                              !strcmp(scenario, "multiple_clients") ? 1800000 :
                              !strcmp(scenario, "reconnect") ? 600000 :
                              !strcmp(scenario, "query_error") ? 360000 :
                              !strcmp(scenario, "repeat") ? 599000 :
                              !strcmp(scenario, "stop_failure") ? 302000 : 300000;
        assert(elapsed == expected);
    }
    assert(!bc250_wifi_is_config_ap() && !led_config_mode && !bc250_wifi_is_connected());
    assert(!zigbee_paused && resume_calls > 0);
    assert(!strcmp(bc250_wifi_ip_address(), "0.0.0.0"));
    if (zigbee || first) assert(!driver_started);
    else {
        assert(driver_started && mode == WIFI_MODE_STA && connect_calls == previous_connects + 1);
        assert(!strcmp((char *)sta_config.sta.ssid, "test-network"));
    }
    puts("AP idle timeout, client activity, radio restoration and LED state passed");
    return 0;
}
