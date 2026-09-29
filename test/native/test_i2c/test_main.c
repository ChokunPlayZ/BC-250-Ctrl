#include <assert.h>
#include <pthread.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/semphr.h"
#include "freertos/task.h"
#include "i2c_service.h"
#include "psu_i2c_service.h"
#include "hp_fru_fixture.h"

struct mock_bus { i2c_master_bus_config_t config; bool alive; unsigned devices; };
struct mock_device { i2c_master_bus_handle_t bus; i2c_device_config_t config; };
static struct mock_bus mock_bus;
static struct mock_device mock_device;
static struct mock_device mock_eeprom;
static uint8_t eeprom[256];
static unsigned eeprom_reads;
static bool eeprom_nack, eeprom_corrupt, eeprom_timeout;
static bc250_i2c_scan_progress_t scan_progress;
static uint16_t stuck_address;
static unsigned creates, deletes, resets, probes, writes, reads, task_creates;
static esp_err_t create_error, reset_error;
static bool mutex_busy, task_failure, sda_low, scl_low, corrupt, nack, permanent_timeout, drop_clock_on_write;
static unsigned probe_timeouts, write_timeouts, read_timeouts;
static uint8_t reg;
static bool waiting_reply;
static void (*captured_task)(void *);
static jmp_buf task_exit;
static unsigned polls_to_run;
static int64_t now_us = 1000000;
static bc250_psu_i2c_status_t samples[4];
static unsigned sample_count;
static pthread_mutex_t race_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t race_changed = PTHREAD_COND_INITIALIZER;
static bool pause_scan, probe_entered, release_scan, startup_attempted;

void mock_log(const char *tag, const char *format, ...) { (void)tag; (void)format; }
const char *esp_err_to_name(esp_err_t err)
{
    switch (err) {
    case ESP_OK: return "ESP_OK";
    case ESP_ERR_TIMEOUT: return "ESP_ERR_TIMEOUT";
    case ESP_ERR_INVALID_RESPONSE: return "ESP_ERR_INVALID_RESPONSE";
    case ESP_ERR_INVALID_STATE: return "ESP_ERR_INVALID_STATE";
    case ESP_ERR_NO_MEM: return "ESP_ERR_NO_MEM";
    default: return "ESP_FAIL";
    }
}
SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t *storage)
{
    assert(pthread_mutex_init(storage, NULL) == 0);
    return storage;
}
BaseType_t xSemaphoreTake(SemaphoreHandle_t semaphore, TickType_t timeout)
{
    assert(semaphore && timeout == 1000);
    if (mutex_busy) return 0;
    assert(pthread_mutex_lock(semaphore) == 0);
    return pdTRUE;
}
BaseType_t xSemaphoreGive(SemaphoreHandle_t semaphore)
{
    assert(pthread_mutex_unlock(semaphore) == 0);
    return pdTRUE;
}
BaseType_t xTaskCreate(void (*task)(void *), const char *name, uint32_t stack, void *arg,
                       unsigned priority, void *handle)
{
    (void)name; (void)stack; (void)arg; (void)priority; (void)handle;
    ++task_creates;
    captured_task = task;
    return task_failure ? 0 : pdPASS;
}
void vTaskDelay(TickType_t ticks)
{
    now_us += ticks * 1000;
    if (ticks >= 500 && --polls_to_run == 0) longjmp(task_exit, 1);
}
int64_t esp_timer_get_time(void) { return now_us; }
void bc250_zigbee_update_psu_status(void)
{
    assert(sample_count < 4);
    samples[sample_count++] = bc250_psu_i2c_service_status();
}
int gpio_get_level(int gpio)
{
    assert(gpio == mock_bus.config.sda_io_num || gpio == mock_bus.config.scl_io_num);
    return gpio == mock_bus.config.sda_io_num ? !sda_low : !scl_low;
}
esp_err_t i2c_new_master_bus(const i2c_master_bus_config_t *config, i2c_master_bus_handle_t *bus)
{
    assert(!mock_bus.alive);
    assert(config->flags.enable_internal_pullup);
    ++creates;
    if (create_error) return create_error;
    mock_bus = (struct mock_bus){.config = *config, .alive = true};
    *bus = &mock_bus;
    return ESP_OK;
}
esp_err_t i2c_del_master_bus(i2c_master_bus_handle_t bus)
{
    assert(bus->alive && bus->devices == 0);
    bus->alive = false;
    ++deletes;
    return ESP_OK;
}
esp_err_t i2c_master_bus_add_device(i2c_master_bus_handle_t bus, const i2c_device_config_t *config,
                                    i2c_master_dev_handle_t *device)
{
    assert(bus->alive && bus->devices < 2);
    assert(config->dev_addr_length == I2C_ADDR_BIT_LEN_7);
    assert(config->scl_speed_hz == 100000 && config->scl_wait_us == 20000);
    struct mock_device *created = config->device_address < 0x58 ? &mock_eeprom : &mock_device;
    *created = (struct mock_device){.bus = bus, .config = *config};
    ++bus->devices;
    *device = created;
    return ESP_OK;
}
esp_err_t i2c_master_bus_rm_device(i2c_master_dev_handle_t device)
{
    assert(device->bus->devices > 0);
    --device->bus->devices;
    return ESP_OK;
}
esp_err_t i2c_master_bus_reset(i2c_master_bus_handle_t bus)
{
    assert(bus->alive);
    ++resets;
    if (!reset_error) waiting_reply = false;
    return reset_error;
}
esp_err_t i2c_master_probe(i2c_master_bus_handle_t bus, uint16_t address, int timeout)
{
    assert(bus->alive && timeout == 100);
    assert(!waiting_reply); // Scans must not split the PSU command/read pair.
    ++probes;
    pthread_mutex_lock(&race_lock);
    if (pause_scan && !probe_entered) {
        probe_entered = true;
        pthread_cond_broadcast(&race_changed);
        while (!release_scan) pthread_cond_wait(&race_changed, &race_lock);
    }
    pthread_mutex_unlock(&race_lock);
    if (stuck_address && address == stuck_address) { scl_low = true; return ESP_ERR_TIMEOUT; }
    if (permanent_timeout) { now_us += 100000; return ESP_ERR_TIMEOUT; }
    if (probe_timeouts) { --probe_timeouts; now_us += 100000; return ESP_ERR_TIMEOUT; }
    return address == 0x57 || address == 0x5f ? ESP_OK : ESP_ERR_NOT_FOUND;
}
esp_err_t i2c_master_transmit(i2c_master_dev_handle_t device, const uint8_t *data, size_t size, int timeout)
{
    assert(device->bus->alive && size == 2 && timeout == 100);
    assert((uint8_t)((device->config.device_address << 1) + data[0] + data[1]) == 0);
    ++writes;
    if (drop_clock_on_write) { scl_low = true; return ESP_ERR_TIMEOUT; }
    if (nack) return ESP_ERR_INVALID_RESPONSE;
    if (permanent_timeout) return ESP_ERR_TIMEOUT;
    if (write_timeouts) { --write_timeouts; return ESP_ERR_TIMEOUT; }
    reg = data[0];
    waiting_reply = true;
    return ESP_OK;
}
esp_err_t i2c_master_transmit_receive(i2c_master_dev_handle_t device, const uint8_t *write_data,
                                     size_t write_size, uint8_t *read_data, size_t read_size, int timeout)
{
    assert(device == &mock_eeprom && device->config.device_address == 0x57);
    assert(write_size == 1 && read_size == 32 && timeout == 100);
    assert(!waiting_reply && write_data[0] <= 224 && write_data[0] % 32 == 0);
    ++eeprom_reads;
    if (eeprom_nack) return ESP_ERR_INVALID_RESPONSE;
    if (eeprom_timeout || scl_low) return ESP_ERR_TIMEOUT;
    memcpy(read_data, eeprom + write_data[0], read_size);
    if (eeprom_corrupt && write_data[0] == 0) read_data[7] ^= 1;
    return ESP_OK;
}
esp_err_t i2c_master_receive(i2c_master_dev_handle_t device, uint8_t *data, size_t size, int timeout)
{
    assert(device->bus->alive && size == 3 && timeout == 100 && waiting_reply);
    ++reads;
    if (read_timeouts) { --read_timeouts; return ESP_ERR_TIMEOUT; }
    waiting_reply = false;
    uint16_t raw = reg == 0x08 ? 230 * 32 : reg == 0x0e ? 12 * 256 : 128;
    data[0] = (uint8_t)raw;
    data[1] = (uint8_t)(raw >> 8);
    data[2] = (uint8_t)(0U - (data[0] + data[1]) + (corrupt ? 1 : 0));
    return ESP_OK;
}
static bc250_config_t config(void)
{
    bc250_config_t c = {0};
    c.ps_on.gpio = c.power_button.gpio = c.power_sense.gpio = c.status_led.gpio = -1;
    c.psu_i2c = (bc250_psu_i2c_config_t){.enabled = true, .sda_gpio = 1, .scl_gpio = 2,
                                      .address = 0x5f, .poll_interval_ms = 2000};
    return c;
}
static void poll(unsigned count)
{
    polls_to_run = count;
    if (!setjmp(task_exit)) captured_task(NULL);
}
static esp_err_t scan(int sda, int scl, uint8_t *addresses, size_t capacity, size_t *count, char *error)
{
    return bc250_i2c_service_scan(sda, scl, addresses, capacity, count, error, BC250_I2C_ERROR_SIZE, &scan_progress);
}
static void *scan_thread(void *unused)
{
    (void)unused;
    uint8_t addresses[112]; size_t count; char error[BC250_I2C_ERROR_SIZE];
    assert(scan(1, 2, addresses, sizeof(addresses), &count, error) == ESP_OK && count == 2);
    return NULL;
}
static void *startup_thread(void *unused)
{
    (void)unused;
    pthread_mutex_lock(&race_lock);
    startup_attempted = true;
    pthread_cond_broadcast(&race_changed);
    pthread_mutex_unlock(&race_lock);
    assert(bc250_i2c_service_start(1, 2) == ESP_OK);
    return NULL;
}
int main(int argc, char **argv)
{
    assert(argc == 2);
    const char *scenario = argv[1];
    uint8_t addresses[112]; size_t count = 99; char error[BC250_I2C_ERROR_SIZE];
    bc250_config_t c = config();
    hp_fru_fixture(eeprom);
    if (!strcmp(scenario, "scan") || !strcmp(scenario, "scan_clock_low") ||
        !strcmp(scenario, "scan_recovery") ||
        !strcmp(scenario, "scan_stuck") || !strcmp(scenario, "scan_isolated_timeout") ||
        !strcmp(scenario, "scan_partial") || !strcmp(scenario, "scan_budget") ||
        !strcmp(scenario, "reset_failure") || !strcmp(scenario, "capacity")) {
        if (!strcmp(scenario, "scan_recovery")) probe_timeouts = 1;
        if (!strcmp(scenario, "scan_clock_low")) scl_low = true;
        if (!strcmp(scenario, "scan_stuck") || !strcmp(scenario, "reset_failure")) {
            permanent_timeout = true; sda_low = true;
        }
        if (!strcmp(scenario, "reset_failure")) reset_error = ESP_FAIL;
        if (!strcmp(scenario, "scan_isolated_timeout")) probe_timeouts = 2;
        if (!strcmp(scenario, "scan_partial")) stuck_address = 0x60;
        if (!strcmp(scenario, "scan_budget")) permanent_timeout = true;
        size_t capacity = !strcmp(scenario, "capacity") ? 1 : sizeof(addresses);
        esp_err_t err = scan(1, 2, addresses, capacity, &count, error);
        if (!strcmp(scenario, "scan_clock_low")) {
            assert(err == ESP_ERR_TIMEOUT && count == 0 && probes == 0 && resets == 0);
            assert(scan_progress.blocked_before_scan && scan_progress.scanned_addresses == 0);
            assert(strstr(error, "before first probe (clock held low"));
        } else if (!strcmp(scenario, "scan_partial")) {
            assert(err == ESP_ERR_TIMEOUT && count == 2 && scan_progress.scanned_addresses == 89);
            assert(strstr(error, "clock held low") && addresses[1] == 0x5f && resets == 0);
        } else if (!strcmp(scenario, "scan_budget")) {
            assert(err == ESP_ERR_TIMEOUT && count == 0 && scan_progress.scanned_addresses == 15);
            assert(scan_progress.timeout_count == 15 && probes == 30 && strstr(error, "time limit"));
        } else if (!strcmp(scenario, "scan_isolated_timeout")) {
            assert(err == ESP_ERR_TIMEOUT && count == 2 && scan_progress.scanned_addresses == 112);
            assert(scan_progress.timeout_count == 1 && addresses[0] == 0x57 && addresses[1] == 0x5f);
        } else if (permanent_timeout) {
            assert(err == ESP_ERR_TIMEOUT && count == 0 && resets == 1);
            assert(probes == 1 && scan_progress.scanned_addresses == 1);
            assert(strstr(error, "0x08") && strstr(error, "SDA GPIO 1=low") && strstr(error, "SCL GPIO 2=high"));
        } else if (capacity == 1) {
            assert(err == ESP_ERR_INVALID_SIZE && count == 1 && addresses[0] == 0x57);
        } else {
            assert(err == ESP_OK && count == 2 && addresses[0] == 0x57 && addresses[1] == 0x5f);
            assert(!error[0] && resets == (!strcmp(scenario, "scan_recovery") ? 1U : 0U));
        }
        assert(creates == 1 && deletes == 1 && !mock_bus.alive);
        permanent_timeout = false; reset_error = 0; sda_low = false; scl_low = false; stuck_address = 0;
        assert(scan(4, 5, addresses, sizeof(addresses), &count, error) == ESP_OK);
        assert(creates == 2 && deletes == 2);
    } else if (!strcmp(scenario, "shared")) {
        assert(bc250_i2c_service_start(1, 2) == ESP_OK);
        assert(bc250_i2c_service_start(1, 2) == ESP_OK);
        assert(bc250_i2c_service_start(4, 5) == ESP_ERR_INVALID_STATE);
        assert(scan(4, 5, addresses, sizeof(addresses), &count, error) == ESP_ERR_INVALID_STATE && count == 0);
        assert(scan(1, 2, addresses, sizeof(addresses), &count, error) == ESP_OK && count == 2);
        assert(creates == 1 && deletes == 0);
        i2c_master_dev_handle_t device;
        assert(bc250_i2c_service_add_device(0x5f, 100000, &device) == ESP_OK);
        bc250_i2c_service_remove_device(device);
        assert(mock_bus.devices == 0);
    } else if (!strcmp(scenario, "busy")) {
        mutex_busy = true;
        assert(scan(1, 2, addresses, sizeof(addresses), &count, error) == ESP_ERR_TIMEOUT);
        assert(count == 0 && creates == 0 && strstr(error, "busy"));
        mutex_busy = false;
        assert(scan(1, 2, addresses, sizeof(addresses), &count, error) == ESP_OK);
    } else if (!strcmp(scenario, "invalid")) {
        assert(scan(1, 1, addresses, sizeof(addresses), &count, error) == ESP_ERR_INVALID_ARG);
        assert(scan(1, 31, addresses, sizeof(addresses), &count, error) == ESP_ERR_INVALID_ARG);
        i2c_master_dev_handle_t device = &mock_device;
        assert(bc250_i2c_service_add_device(0x5f, 0, &device) == ESP_ERR_INVALID_ARG && device == NULL);
        assert(creates == 0 && count == 0);
    } else if (!strcmp(scenario, "create_failure")) {
        create_error = ESP_ERR_NO_MEM;
        assert(bc250_i2c_service_start(1, 2) == ESP_ERR_NO_MEM);
        create_error = 0;
        assert(bc250_i2c_service_start(4, 5) == ESP_OK && creates == 2);
    } else if (!strcmp(scenario, "concurrent_start")) {
        pthread_t scanning, starting;
        pause_scan = true;
        assert(pthread_create(&scanning, NULL, scan_thread, NULL) == 0);
        pthread_mutex_lock(&race_lock);
        while (!probe_entered) pthread_cond_wait(&race_changed, &race_lock);
        pthread_mutex_unlock(&race_lock);
        assert(pthread_create(&starting, NULL, startup_thread, NULL) == 0);
        pthread_mutex_lock(&race_lock);
        while (!startup_attempted) pthread_cond_wait(&race_changed, &race_lock);
        release_scan = true;
        pthread_cond_broadcast(&race_changed);
        pthread_mutex_unlock(&race_lock);
        pthread_join(scanning, NULL); pthread_join(starting, NULL);
        assert(creates == 2 && deletes == 1 && mock_bus.alive);
    } else if (!strcmp(scenario, "task_failure")) {
        task_failure = true;
        assert(bc250_psu_i2c_service_start(&c) == ESP_ERR_NO_MEM && mock_bus.devices == 0);
        bc250_psu_i2c_status_t status = bc250_psu_i2c_service_status();
        assert(status.enabled && !status.available && strstr(status.error, "ESP_ERR_NO_MEM"));
        task_failure = false;
        assert(bc250_psu_i2c_service_start(&c) == ESP_OK && mock_bus.devices == 1);
    } else {
        if (!strcmp(scenario, "psu_write_recovery")) write_timeouts = 1;
        else if (!strcmp(scenario, "psu_read_recovery")) read_timeouts = 1;
        else if (!strcmp(scenario, "psu_stuck")) { permanent_timeout = true; scl_low = true; }
        else if (!strcmp(scenario, "psu_clock_drops")) drop_clock_on_write = true;
        else if (!strcmp(scenario, "psu_nack")) nack = true;
        else if (!strcmp(scenario, "psu_crc")) corrupt = true;
        else if (!strcmp(scenario, "psu_identity_nack")) eeprom_nack = true;
        else if (!strcmp(scenario, "psu_identity_crc")) eeprom_corrupt = true;
        else if (!strcmp(scenario, "psu_identity_timeout")) eeprom_timeout = true;
        else assert(!strcmp(scenario, "psu") || !strcmp(scenario, "psu_restored") || !strcmp(scenario, "psu_identity_cache"));
        if (!strcmp(scenario, "psu_restored")) corrupt = true;
        assert(bc250_psu_i2c_service_start(&c) == ESP_OK);
        assert(bc250_psu_i2c_service_start(&c) == ESP_ERR_INVALID_STATE && task_creates == 1);
        poll(!strcmp(scenario, "psu_identity_cache") ? 2 : 1);
        bc250_psu_i2c_status_t status = bc250_psu_i2c_service_status();
        if (corrupt || nack || permanent_timeout || drop_clock_on_write) {
            assert(status.enabled && !status.available && strstr(status.error, "0x5F") && strstr(status.error, "register 0x08"));
            if (corrupt) assert(strstr(status.error, "checksum") && resets == 0 && writes == 1 && reads == 1);
            if (nack) assert(strstr(status.error, "PIC address") && resets == 0 && writes == 1 && reads == 0);
            if (permanent_timeout) {
                assert(strstr(status.error, "SCL GPIO 2=low") && strstr(status.error, "skipped"));
                assert(resets == 0 && writes == 0 && reads == 0 && eeprom_reads == 0);
                assert(!status.identity.available && strstr(status.identity.error, "clock held low"));
            }
            if (drop_clock_on_write) {
                assert(strstr(status.error, "SCL GPIO 2=low") && resets == 0 && writes == 1);
                assert(eeprom_reads == 0);
                drop_clock_on_write = false; scl_low = false;
                poll(1); status = bc250_psu_i2c_service_status();
                assert(status.available && !status.error[0] && sample_count == 2);
            }
            if (!strcmp(scenario, "psu_restored")) {
                corrupt = false; poll(1); status = bc250_psu_i2c_service_status();
                assert(status.available && !status.error[0] && sample_count == 2);
            }
        } else {
            assert(status.available && !status.error[0]);
            assert(status.input_voltage_v == 230.0f && status.output_voltage_v == 12.0f);
            unsigned successful = !strcmp(scenario, "psu_identity_cache") ? 12 : 6;
            assert(reads == successful + (!strcmp(scenario, "psu_read_recovery") ? 1U : 0U));
            assert(writes == successful + (strstr(scenario, "_recovery") ? 1U : 0U));
            if (eeprom_nack || eeprom_corrupt || eeprom_timeout) {
                assert(!status.identity.available && status.identity.error[0] && !status.error[0]);
                if (eeprom_timeout) assert(eeprom_reads == 2 && resets == 1);
            } else {
                assert(status.identity.available && !strcmp(status.identity.data.manufacturer, "DELTA"));
                assert(status.identity.data.rated_capacity_w == 1200 && eeprom_reads == 8);
            }
        }
        assert(scan(1, 2, addresses, sizeof(addresses), &count, error) == (permanent_timeout ? ESP_ERR_TIMEOUT : ESP_OK));
        if (permanent_timeout) assert(scan_progress.blocked_before_scan && scan_progress.scanned_addresses == 0 && probes == 0);
    }
    printf("I2C scenario %s passed\n", scenario);
    return 0;
}
