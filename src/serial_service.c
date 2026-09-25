#include "serial_service.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>

#include "ble_presence.h"
#include "cJSON.h"
#include "config_store.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "i2c_service.h"
#include "nvs_flash.h"
#include "power_service.h"
#include "sdkconfig.h"
#include "status_service.h"
#include "wifi_service.h"
#include "zigbee_service.h"

#if CONFIG_ESP_CONSOLE_UART
#include "driver/uart.h"
#include "driver/uart_vfs.h"
#elif CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#endif

#define SERIAL_LINE_MAX 12288

static const char *TAG = "serial";

static void reply_json(const char *json)
{
    printf("BC250 {\"ok\":true,\"data\":%s}\n", json);
    fflush(stdout);
}

static void reply_error(const char *message)
{
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        puts("BC250 {\"ok\":false,\"error\":\"Out of memory\"}");
        return;
    }
    cJSON_AddBoolToObject(root, "ok", false);
    cJSON_AddStringToObject(root, "error", message);
    char *json = cJSON_PrintUnformatted(root);
    if (json != NULL) {
        printf("BC250 %s\n", json);
        cJSON_free(json);
    }
    cJSON_Delete(root);
    fflush(stdout);
}

static void reply_result(esp_err_t err)
{
    if (err == ESP_OK) reply_json("{\"accepted\":true}");
    else reply_error(esp_err_to_name(err));
}

static void reply_owned_json(char *json)
{
    if (json == NULL) reply_error("Out of memory");
    else {
        reply_json(json);
        cJSON_free(json);
    }
}

static bc250_power_action_t power_action(const char *name)
{
    if (strcmp(name, "on") == 0) return BC250_POWER_ACTION_ON;
    if (strcmp(name, "off") == 0) return BC250_POWER_ACTION_OFF;
    if (strcmp(name, "toggle") == 0) return BC250_POWER_ACTION_TOGGLE;
    if (strcmp(name, "force_off") == 0) return BC250_POWER_ACTION_FORCE_OFF;
    return BC250_POWER_ACTION_NONE;
}

static void scan_i2c(const char *args)
{
    int sda, scl;
    char extra;
    if (sscanf(args, "%d %d %c", &sda, &scl, &extra) != 2 ||
        sda < 0 || sda > 31 || scl < 0 || scl > 31) {
        reply_error("Usage: i2c scan <sda_gpio> <scl_gpio>");
        return;
    }
    char error[160];
    if (bc250_config_validate_i2c_pins(bc250_config_get(), sda, scl,
                                       error, sizeof(error)) != ESP_OK) {
        reply_error(error);
        return;
    }
    uint8_t addresses[BC250_I2C_MAX_SCAN_ADDRESSES];
    size_t count = 0;
    esp_err_t err = bc250_i2c_service_scan(sda, scl, addresses, sizeof(addresses), &count);
    if (err != ESP_OK) {
        reply_error(err == ESP_ERR_INVALID_STATE ? "Active I2C bus uses different GPIOs" :
                    err == ESP_ERR_TIMEOUT ? "I2C bus timed out; check wiring and pull-ups" :
                    esp_err_to_name(err));
        return;
    }
    cJSON *root = cJSON_CreateObject();
    cJSON *found = root ? cJSON_AddArrayToObject(root, "addresses") : NULL;
    if (found != NULL) {
        for (size_t i = 0; i < count; ++i) {
            cJSON_AddItemToArray(found, cJSON_CreateNumber(addresses[i]));
        }
    }
    char *json = found && (size_t)cJSON_GetArraySize(found) == count ?
                 cJSON_PrintUnformatted(root) : NULL;
    cJSON_Delete(root);
    reply_owned_json(json);
}

static void handle_command(char *line)
{
    while (*line == ' ' || *line == '\t') ++line;
    if (*line == '\0') return;
    if (strcmp(line, "help") == 0) {
        reply_json("{\"commands\":[\"status\",\"config get\",\"config set <JSON patch>\","
                   "\"power on|off|toggle|force_off\",\"ble scan\",\"ble results\","
                   "\"i2c scan <SDA> <SCL>\",\"zigbee commission|reset\",\"wifi ap\","
                   "\"admin reset\",\"factory reset ERASE ALL\",\"reboot\"]}");
    } else if (strcmp(line, "status") == 0) {
        reply_owned_json(bc250_status_json());
    } else if (strcmp(line, "config get") == 0) {
        reply_owned_json(bc250_config_to_json(bc250_config_get(), false));
    } else if (strncmp(line, "config set ", 11) == 0) {
        bc250_config_t *next = malloc(sizeof(*next));
        if (next == NULL) {
            reply_error("Out of memory");
            return;
        }
        *next = *bc250_config_get();
        char error[160] = "";
        esp_err_t err = bc250_config_patch_json(next, line + 11, error, sizeof(error));
        if (err == ESP_OK) err = bc250_config_save_pending(next);
        free(next);
        if (err != ESP_OK) {
            reply_error(error[0] ? error : esp_err_to_name(err));
        } else {
            reply_json("{\"accepted\":true,\"rebooting\":true}");
            vTaskDelay(pdMS_TO_TICKS(750));
            esp_restart();
        }
    } else if (strncmp(line, "power ", 6) == 0) {
        bc250_power_action_t action = power_action(line + 6);
        if (action == BC250_POWER_ACTION_NONE) reply_error("Usage: power on|off|toggle|force_off");
        else if (!bc250_power_service_request(action)) reply_error("Power command unavailable or queue full");
        else reply_json("{\"accepted\":true}");
    } else if (strcmp(line, "ble scan") == 0) {
        esp_err_t err = bc250_ble_start_learning(15000);
        if (err == ESP_OK) reply_json("{\"accepted\":true,\"duration_ms\":15000}");
        else reply_error(esp_err_to_name(err));
    } else if (strcmp(line, "ble results") == 0) {
        reply_owned_json(bc250_ble_scan_results_json());
    } else if (strncmp(line, "i2c scan ", 9) == 0) {
        scan_i2c(line + 9);
    } else if (strcmp(line, "zigbee commission") == 0) {
        reply_result(bc250_zigbee_commission());
    } else if (strcmp(line, "zigbee reset") == 0) {
        reply_result(bc250_zigbee_factory_reset());
    } else if (strcmp(line, "wifi ap") == 0) {
        reply_result(bc250_wifi_open_config_ap());
    } else if (strcmp(line, "admin reset") == 0) {
        char password[17];
        esp_err_t err = bc250_config_reset_admin_password(password);
        if (err == ESP_OK) {
            printf("BC250 {\"ok\":true,\"data\":{\"admin_password\":\"%s\"}}\n", password);
            fflush(stdout);
        } else reply_error(esp_err_to_name(err));
        memset(password, 0, sizeof(password));
    } else if (strcmp(line, "factory reset ERASE ALL") == 0) {
        esp_err_t err = nvs_flash_erase();
        if (err == ESP_OK) {
            reply_json("{\"accepted\":true,\"rebooting\":true}");
            vTaskDelay(pdMS_TO_TICKS(750));
            esp_restart();
        } else reply_error(esp_err_to_name(err));
    } else if (strcmp(line, "reboot") == 0) {
        reply_json("{\"accepted\":true,\"rebooting\":true}");
        vTaskDelay(pdMS_TO_TICKS(750));
        esp_restart();
    } else {
        reply_error("Unknown command; enter help");
    }
}

static void serial_task(void *arg)
{
    (void)arg;
    char *line = malloc(SERIAL_LINE_MAX + 1);
    if (line == NULL) {
        ESP_LOGE(TAG, "Unable to allocate command buffer");
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "Serial commands ready; enter help");
    size_t used = 0;
    bool overflow = false;
    while (true) {
        int ch = getchar();
        if (ch == EOF) {
            clearerr(stdin);
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        if (ch == '\r' || ch == '\n') {
            if (overflow) reply_error("Command exceeds 12288 bytes");
            else if (used > 0) {
                line[used] = '\0';
                handle_command(line);
                memset(line, 0, used);
            }
            used = 0;
            overflow = false;
        } else if (!overflow) {
            if (used < SERIAL_LINE_MAX) line[used++] = (char)ch;
            else overflow = true;
        }
    }
}

esp_err_t bc250_serial_service_start(void)
{
#if CONFIG_ESP_CONSOLE_UART
    esp_err_t err = uart_driver_install(CONFIG_ESP_CONSOLE_UART_NUM, 1024, 0, 0, NULL, 0);
    if (err != ESP_OK) return err;
    uart_vfs_dev_use_driver(CONFIG_ESP_CONSOLE_UART_NUM);
#elif CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
    usb_serial_jtag_driver_config_t config = {.tx_buffer_size = 512, .rx_buffer_size = 1024};
    esp_err_t err = usb_serial_jtag_driver_install(&config);
    if (err != ESP_OK) return err;
    usb_serial_jtag_vfs_use_driver();
    if (fcntl(fileno(stdin), F_SETFL, 0) < 0) return ESP_FAIL;
#elif !CONFIG_ESP_CONSOLE_USB_CDC
    return ESP_ERR_NOT_SUPPORTED;
#endif
    setvbuf(stdin, NULL, _IONBF, 0);
    return xTaskCreate(serial_task, "serial", 8192, NULL, 3, NULL) == pdPASS ?
           ESP_OK : ESP_ERR_NO_MEM;
}
