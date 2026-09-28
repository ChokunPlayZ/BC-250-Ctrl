#include "serial_service.h"

#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ble_presence.h"
#include "config_store.h"
#include "core/shell_args.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "i2c_service.h"
#include "linenoise/linenoise.h"
#include "nvs_flash.h"
#include "power_service.h"
#include "psu_i2c_service.h"
#include "sdkconfig.h"
#include "serial_config.h"
#include "wifi_service.h"
#include "zigbee_service.h"

#if CONFIG_ESP_CONSOLE_UART
#include "driver/uart.h"
#include "driver/uart_vfs.h"
#elif CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#elif CONFIG_ESP_CONSOLE_USB_CDC
#include "esp_vfs_cdcacm.h"
#endif

#define SERIAL_LINE_MAX 512

static const char *TAG = "serial";
static bc250_config_t *s_edit;
static bool s_dirty;

static const char *const commands[] = {
    "help", "status", "config", "set", "save", "discard",
    "on", "off", "toggle", "force-off", "power on", "power off", "power toggle", "power force-off",
    "config get", "config set", "config save", "config discard",
    "ble scan", "ble results", "i2c scan", "zigbee commission", "zigbee reset",
    "wifi ap", "admin reset", "factory reset ERASE ALL", "reboot",
    "logs on", "logs off", "terminal plain", "terminal ansi",
};

static void reply_error(const char *message)
{
    printf("Error: %s\n", message);
}

static void reply_result(esp_err_t err, const char *success)
{
    if (err == ESP_OK) puts(success);
    else reply_error(esp_err_to_name(err));
}

static void reboot(void)
{
    fflush(stdout);
    vTaskDelay(pdMS_TO_TICKS(750));
    esp_restart();
}

static bc250_power_action_t power_action(const char *name)
{
    if (!strcmp(name, "on")) return BC250_POWER_ACTION_ON;
    if (!strcmp(name, "off")) return BC250_POWER_ACTION_OFF;
    if (!strcmp(name, "toggle")) return BC250_POWER_ACTION_TOGGLE;
    if (!strcmp(name, "force-off") || !strcmp(name, "force_off")) return BC250_POWER_ACTION_FORCE_OFF;
    return BC250_POWER_ACTION_NONE;
}

static void print_help(void)
{
    puts("Commands:\n"
         "  status                         Show power, network and PSU status\n"
         "  on | off | toggle | force-off   Control power (power <action> also works)\n"
         "  config                         List settings and unsaved changes\n"
         "  set <setting> <value>          Edit a setting; Tab completes names\n"
         "  save                           Validate, save changes and reboot\n"
         "  discard                        Discard unsaved changes\n"
         "  ble scan | ble results         Discover BLE devices for 15 seconds\n"
         "  i2c scan <SDA> <SCL>            Scan I2C pins\n"
         "  zigbee commission | reset      Join or reset the Zigbee network\n"
         "  wifi ap                        Open the setup access point\n"
         "  admin reset                    Generate and display a new admin password\n"
         "  factory reset ERASE ALL         Erase all settings and reboot\n"
         "  reboot                         Restart; discards unsaved changes\n"
         "  logs on | off                  Show service logs or only errors\n"
         "  terminal plain | ansi          Select basic or full line editing\n"
         "  help                           Show this help\n\n"
         "Examples:\n"
         "  set hostname bc250-lab\n"
         "  set wifi_ssid \"My Wi-Fi\"\n"
         "  set radio hybrid\n"
         "  set ps_on.gpio 4\n"
         "  set psu_i2c.address 0x5f\n"
         "  save\n\n"
         "Use on/off for booleans, -1 to disable a GPIO, and quotes for spaces.\n"
         "Edits take effect only after save. A * in the prompt means unsaved changes.");
}

static void print_status(void)
{
    bc250_power_outputs_t outputs = bc250_power_service_outputs();
    printf("Firmware:      %s\n", BC250_VERSION);
    printf("Power:         %s (sense: %s)\n", bc250_power_state_name(bc250_power_service_state()),
           bc250_power_service_sensed_on() ? "on" : "off");
    printf("Outputs:       PS_ON %s, power button %s\n", outputs.ps_on ? "active" : "inactive",
           outputs.power_button ? "active" : "inactive");
    printf("Wi-Fi:         %s\n", bc250_wifi_is_connected() ? "connected" : "disconnected");
    printf("IP address:    %s\n", bc250_wifi_ip_address());
    printf("Setup AP:      %s\n", bc250_wifi_is_config_ap() ? "open at http://192.168.4.1/" : "closed");
    printf("Zigbee:        %s\n", bc250_zigbee_is_joined() ? "joined" :
           bc250_zigbee_is_started() ? "not joined" : "disabled");
#ifdef CONFIG_BC250_OTA_ENABLED
    puts("OTA:           available");
#else
    puts("OTA:           unavailable");
#endif
    printf("BLE present:  ");
    const bc250_config_t *config = bc250_config_get();
    unsigned present = 0;
    for (unsigned i = 0; i < config->ble_device_count; ++i) {
        if (bc250_ble_device_present(i)) {
            printf("%s%s", present++ ? ", " : " ", config->ble_devices[i].label);
        }
    }
    puts(present ? "" : " none");
    bc250_psu_i2c_status_t psu = bc250_psu_i2c_service_status();
    if (!psu.enabled) puts("PSU I2C:       disabled");
    else if (!psu.available) puts("PSU I2C:       unavailable; check wiring and address");
    else {
        printf("PSU input:     %.2f V, %.2f A\n", (double)psu.input_voltage_v, (double)psu.input_current_a);
        printf("PSU output:    %.2f V, %.2f A\n", (double)psu.output_voltage_v, (double)psu.output_current_a);
        printf("PSU temp:      %.1f F\n", (double)psu.internal_temperature_f);
        printf("PSU fan:       %u (raw)\n", psu.fan_speed_raw);
        printf("PSU sample:    %" PRIu32 " ms old\n", psu.age_ms);
    }
}

static void print_ble_results(void)
{
    puts("Address            RSSI    Type     Name");
    unsigned count = 0;
    for (unsigned i = 0; i < BC250_BLE_SCAN_RESULT_COUNT; ++i) {
        bc250_ble_scan_result_t result;
        if (!bc250_ble_scan_result(i, &result)) continue;
        printf("%-18s %4d    %-8s %s%s\n", result.address, result.rssi,
               result.address_type == 0 ? "public" : "random",
               result.name[0] ? result.name : "(unnamed)", result.address_may_rotate ? " (address may rotate)" : "");
        ++count;
    }
    if (!count) puts("No devices found. Run ble scan, wait 15 seconds, then ble results.");
}

static bool parse_gpio(const char *value, int *gpio)
{
    char *end;
    long number = strtol(value, &end, 10);
    if (end == value || *end || number < 0 || number > 31) return false;
    *gpio = (int)number;
    return true;
}

static void scan_i2c(const char *sda_arg, const char *scl_arg)
{
    int sda, scl;
    if (!parse_gpio(sda_arg, &sda) || !parse_gpio(scl_arg, &scl)) {
        reply_error("Usage: i2c scan <sda_gpio> <scl_gpio> (0-31)");
        return;
    }
    char error[160];
    if (bc250_config_validate_i2c_pins(bc250_config_get(), sda, scl, error, sizeof(error)) != ESP_OK) {
        reply_error(error);
        return;
    }
    uint8_t addresses[BC250_I2C_MAX_SCAN_ADDRESSES];
    size_t count = 0;
    esp_err_t err = bc250_i2c_service_scan(sda, scl, addresses, sizeof(addresses), &count);
    if (err != ESP_OK) {
        reply_error(err == ESP_ERR_INVALID_STATE ? "Active I2C bus uses different GPIOs" :
                    err == ESP_ERR_TIMEOUT ? "I2C bus timed out; check wiring and pull-ups" : esp_err_to_name(err));
        return;
    }
    if (!count) puts("No I2C devices found.");
    else {
        printf("Found %zu I2C device%s:", count, count == 1 ? "" : "s");
        for (size_t i = 0; i < count; ++i) printf(" 0x%02X (%u)", addresses[i], addresses[i]);
        putchar('\n');
    }
}

static void edit_setting(const char *key, const char *value)
{
    if (s_edit == NULL) {
        s_edit = malloc(sizeof(*s_edit));
        if (s_edit == NULL) { reply_error("Out of memory"); return; }
        *s_edit = *bc250_config_get();
    }
    char error[160];
    if (bc250_serial_config_set(s_edit, key, value, error, sizeof(error)) != ESP_OK) reply_error(error);
    else { s_dirty = true; puts("Setting updated. Enter save to apply, or discard to undo."); }
}

static void save_config(void)
{
    if (!s_dirty) { puts("No unsaved changes."); return; }
    char error[160] = "";
    esp_err_t err = bc250_config_validate(s_edit, error, sizeof(error));
    if (err == ESP_OK) err = bc250_config_save_pending(s_edit);
    if (err != ESP_OK) { reply_error(error[0] ? error : esp_err_to_name(err)); return; }
    puts("Settings saved. Rebooting...");
    reboot();
}

static void discard_config(void)
{
    if (s_edit) { memset(s_edit, 0, sizeof(*s_edit)); free(s_edit); s_edit = NULL; }
    s_dirty = false;
    puts("Unsaved changes discarded.");
}

static void handle_command(char *line)
{
    char *argv[6];
    int argc = bc250_shell_split(line, argv, sizeof(argv) / sizeof(argv[0]));
    if (argc < 0) { reply_error("Check quotes, escapes and argument count; enter help for examples"); return; }
    if (!argc) return;
    const char *command = argv[0];
    if (argc == 1 && (!strcmp(command, "help") || !strcmp(command, "?"))) print_help();
    else if (argc == 1 && !strcmp(command, "status")) print_status();
    else if (!strcmp(command, "config") && (argc == 1 || (argc == 2 &&
             (!strcmp(argv[1], "get") || !strcmp(argv[1], "show"))))) {
        puts(s_dirty ? "Settings (including unsaved edits):" : "Current settings:");
        bc250_serial_config_print(s_dirty ? s_edit : bc250_config_get());
        puts("Edit with set <setting> <value>; save applies all changes.");
    } else if (!strcmp(command, "set") && argc == 3) edit_setting(argv[1], argv[2]);
    else if (!strcmp(command, "config") && argc == 4 && !strcmp(argv[1], "set")) edit_setting(argv[2], argv[3]);
    else if ((argc == 1 && !strcmp(command, "save")) ||
             (argc == 2 && !strcmp(command, "config") && !strcmp(argv[1], "save"))) save_config();
    else if ((argc == 1 && !strcmp(command, "discard")) ||
             (argc == 2 && !strcmp(command, "config") && !strcmp(argv[1], "discard"))) discard_config();
    else if ((argc == 1 && power_action(command) != BC250_POWER_ACTION_NONE) ||
             (argc == 2 && !strcmp(command, "power"))) {
        bc250_power_action_t action = power_action(argc == 1 ? command : argv[1]);
        if (action == BC250_POWER_ACTION_NONE) reply_error("Usage: power on|off|toggle|force-off");
        else if (!bc250_power_service_request(action)) reply_error("Power command unavailable or queue full");
        else puts("Power command queued. Enter status to check the result.");
    } else if (argc == 2 && !strcmp(command, "ble") && !strcmp(argv[1], "scan")) {
        reply_result(bc250_ble_start_learning(15000), "Scanning for 15 seconds. Then enter ble results.");
    } else if (argc == 2 && !strcmp(command, "ble") && !strcmp(argv[1], "results")) print_ble_results();
    else if (argc == 4 && !strcmp(command, "i2c") && !strcmp(argv[1], "scan")) scan_i2c(argv[2], argv[3]);
    else if (argc == 2 && !strcmp(command, "zigbee") && !strcmp(argv[1], "commission")) {
        reply_result(bc250_zigbee_commission(), "Zigbee joining started.");
    } else if (argc == 2 && !strcmp(command, "zigbee") && !strcmp(argv[1], "reset")) {
        reply_result(bc250_zigbee_factory_reset(), "Zigbee network reset.");
    } else if (argc == 2 && !strcmp(command, "wifi") && !strcmp(argv[1], "ap")) {
        reply_result(bc250_wifi_open_config_ap(), "Setup AP opened. Visit http://192.168.4.1/");
    } else if (argc == 2 && !strcmp(command, "admin") && !strcmp(argv[1], "reset")) {
        char password[17];
        esp_err_t err = bc250_config_reset_admin_password(password);
        if (err == ESP_OK) {
            printf("New admin password: %s\n", password);
            puts("Keep this password; it is shown only once.");
            /* Preserve edits without letting save restore the old password hash. */
            if (s_edit) {
                memcpy(s_edit->admin_salt, bc250_config_get()->admin_salt, sizeof(s_edit->admin_salt));
                memcpy(s_edit->admin_hash, bc250_config_get()->admin_hash, sizeof(s_edit->admin_hash));
            }
        } else reply_error(esp_err_to_name(err));
        memset(password, 0, sizeof(password));
    } else if (argc == 4 && !strcmp(command, "factory") && !strcmp(argv[1], "reset") &&
               !strcmp(argv[2], "ERASE") && !strcmp(argv[3], "ALL")) {
        esp_err_t err = nvs_flash_erase();
        if (err == ESP_OK) { puts("All settings erased. Rebooting..."); reboot(); }
        else reply_error(esp_err_to_name(err));
    } else if (argc == 1 && !strcmp(command, "reboot")) { puts("Rebooting..."); reboot(); }
    else if (argc == 2 && !strcmp(command, "logs") &&
             (!strcmp(argv[1], "on") || !strcmp(argv[1], "off"))) {
        esp_log_level_set("*", !strcmp(argv[1], "on") ? ESP_LOG_INFO : ESP_LOG_ERROR);
        puts(!strcmp(argv[1], "on") ? "Service logs enabled." : "Only errors will be logged.");
    } else if (argc == 2 && !strcmp(command, "terminal") &&
               (!strcmp(argv[1], "plain") || !strcmp(argv[1], "ansi"))) {
        linenoiseSetDumbMode(!strcmp(argv[1], "plain"));
        puts(!strcmp(argv[1], "plain") ? "Basic terminal mode." : "Full line editing enabled.");
    } else reply_error("Unknown command or arguments; enter help");
}

static void complete_command(const char *line, linenoiseCompletions *completions)
{
    size_t length = strlen(line);
    for (size_t i = 0; i < sizeof(commands) / sizeof(commands[0]); ++i) {
        if (strncmp(commands[i], line, length) == 0) linenoiseAddCompletion(completions, commands[i]);
    }
    const char *prefix = strncmp(line, "set ", 4) == 0 ? "set " :
                         strncmp(line, "config set ", 11) == 0 ? "config set " : NULL;
    if (!prefix) return;
    size_t prefix_length = strlen(prefix);
    const char *partial = line + prefix_length;
    if (strchr(partial, ' ')) return;
    char key[80], suggestion[100];
    for (size_t i = 0; bc250_serial_config_key(i, key, sizeof(key)); ++i) {
        if (strncmp(key, partial, strlen(partial)) != 0) continue;
        snprintf(suggestion, sizeof(suggestion), "%s%s ", prefix, key);
        linenoiseAddCompletion(completions, suggestion);
    }
}

/* Basic terminals still get echo, backspace and cancellation without ANSI queries.
 * Drain oversized lines completely so a truncated command cannot execute. */
static char *read_plain_line(const char *prompt)
{
    char *line = calloc(1, SERIAL_LINE_MAX + 1);
    if (!line) return NULL;
    fputs(prompt, stdout);
    fflush(stdout);
    size_t used = 0;
    bool overflow = false;
    while (true) {
        int ch = getchar();
        if (ch == EOF) { clearerr(stdin); vTaskDelay(pdMS_TO_TICKS(20)); continue; }
        if (ch == '\r' || ch == '\n' || ch == 3) {
            putchar('\n');
            if (ch == 3) { memset(line, 0, SERIAL_LINE_MAX + 1); break; }
            if (overflow) { reply_error("Command exceeds 512 characters"); line[0] = '\0'; }
            break;
        }
        if (ch == '\b' || ch == 127) {
            if (!overflow && used) { line[--used] = '\0'; fputs("\b \b", stdout); }
        } else if (ch >= 32 && ch != 127 && !overflow) {
            if (used == SERIAL_LINE_MAX) overflow = true;
            else { line[used++] = (char)ch; putchar(ch); }
        }
        fflush(stdout);
    }
    return line;
}

static bool remember_command(const char *line)
{
    /* Parse a copy so whitespace and quoting cannot bypass history filtering. */
    char copy[SERIAL_LINE_MAX + 1];
    snprintf(copy, sizeof(copy), "%s", line);
    char *argv[6];
    int argc = bc250_shell_split(copy, argv, sizeof(argv) / sizeof(argv[0]));
    return argc > 0 && strcmp(argv[0], "set") &&
           !(argc > 1 && !strcmp(argv[0], "config") && !strcmp(argv[1], "set"));
}

static void serial_task(void *arg)
{
    (void)arg;
    linenoiseSetMultiLine(1);
    linenoiseHistorySetMaxLen(20);
    /* One spare character lets us reject a full/truncated linenoise buffer. */
    linenoiseSetMaxLineLen(SERIAL_LINE_MAX + 2);
    linenoiseSetCompletionCallback(complete_command);
    linenoiseSetDumbMode(linenoiseProbe() != 0);
    esp_log_level_set("*", ESP_LOG_ERROR);
    puts("\nBC250 shell. Enter help for commands.");
    if (linenoiseIsDumbMode()) puts("Basic terminal mode; use terminal ansi for history and Tab completion.");
    while (true) {
        const char *prompt = s_dirty ? "bc250*> " : "bc250> ";
        char *line = linenoiseIsDumbMode() ? read_plain_line(prompt) : linenoise(prompt);
        if (!line) { clearerr(stdin); vTaskDelay(pdMS_TO_TICKS(20)); continue; }
        size_t length = strlen(line);
        if (length > SERIAL_LINE_MAX) reply_error("Command exceeds 512 characters");
        else {
            if (remember_command(line)) linenoiseHistoryAdd(line);
            handle_command(line);
        }
        memset(line, 0, length);
        linenoiseFree(line);
        fflush(stdout);
    }
}

esp_err_t bc250_serial_service_start(void)
{
#if CONFIG_ESP_CONSOLE_UART
    esp_err_t err = uart_driver_install(CONFIG_ESP_CONSOLE_UART_NUM, 1024, 0, 0, NULL, 0);
    if (err != ESP_OK) return err;
    uart_vfs_dev_use_driver(CONFIG_ESP_CONSOLE_UART_NUM);
    uart_vfs_dev_port_set_rx_line_endings(CONFIG_ESP_CONSOLE_UART_NUM, ESP_LINE_ENDINGS_CR);
    uart_vfs_dev_port_set_tx_line_endings(CONFIG_ESP_CONSOLE_UART_NUM, ESP_LINE_ENDINGS_CRLF);
#elif CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
    usb_serial_jtag_driver_config_t config = {.tx_buffer_size = 512, .rx_buffer_size = 1024};
    esp_err_t err = usb_serial_jtag_driver_install(&config);
    if (err != ESP_OK) return err;
    usb_serial_jtag_vfs_use_driver();
    usb_serial_jtag_vfs_set_rx_line_endings(ESP_LINE_ENDINGS_CR);
    usb_serial_jtag_vfs_set_tx_line_endings(ESP_LINE_ENDINGS_CRLF);
    if (fcntl(fileno(stdin), F_SETFL, 0) < 0) return ESP_FAIL;
#elif CONFIG_ESP_CONSOLE_USB_CDC
    esp_vfs_dev_cdcacm_set_rx_line_endings(ESP_LINE_ENDINGS_CR);
    esp_vfs_dev_cdcacm_set_tx_line_endings(ESP_LINE_ENDINGS_CRLF);
#else
    return ESP_ERR_NOT_SUPPORTED;
#endif
    setvbuf(stdin, NULL, _IONBF, 0);
    if (xTaskCreate(serial_task, "serial", 8192, NULL, 3, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Unable to start shell");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
