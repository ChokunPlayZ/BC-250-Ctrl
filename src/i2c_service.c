#include "i2c_service.h"

#include <stdbool.h>
#include <stdio.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "i2c_service";
static i2c_master_bus_handle_t s_bus;
static SemaphoreHandle_t s_mutex;
static StaticSemaphore_t s_mutex_storage;
static portMUX_TYPE s_init_lock = portMUX_INITIALIZER_UNLOCKED;
static int s_sda_gpio = -1;
static int s_scl_gpio = -1;

esp_err_t bc250_i2c_service_lock(void)
{
    // Scans can run before PSU startup. Serialize bus creation/deletion too.
    portENTER_CRITICAL(&s_init_lock);
    if (s_mutex == NULL) s_mutex = xSemaphoreCreateMutexStatic(&s_mutex_storage);
    portEXIT_CRITICAL(&s_init_lock);
    if (s_mutex == NULL) return ESP_ERR_NO_MEM;
    return xSemaphoreTake(s_mutex, pdMS_TO_TICKS(1000)) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

void bc250_i2c_service_unlock(void)
{
    xSemaphoreGive(s_mutex);
}

static esp_err_t new_bus(int sda_gpio, int scl_gpio, i2c_master_bus_handle_t *bus)
{
    if (sda_gpio == scl_gpio || !GPIO_IS_VALID_OUTPUT_GPIO(sda_gpio) ||
        !GPIO_IS_VALID_OUTPUT_GPIO(scl_gpio)) return ESP_ERR_INVALID_ARG;
    i2c_master_bus_config_t config = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = sda_gpio,
        .scl_io_num = scl_gpio,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        // Weak fallback for discovery; external 3.3 V pull-ups are still recommended.
        .flags.enable_internal_pullup = true,
    };
    return i2c_new_master_bus(&config, bus);
}

esp_err_t bc250_i2c_service_start(int sda_gpio, int scl_gpio)
{
    esp_err_t err = bc250_i2c_service_lock();
    if (err != ESP_OK) return err;
    if (s_bus != NULL) {
        err = sda_gpio == s_sda_gpio && scl_gpio == s_scl_gpio ? ESP_OK : ESP_ERR_INVALID_STATE;
    } else {
        i2c_master_bus_handle_t bus = NULL;
        err = new_bus(sda_gpio, scl_gpio, &bus);
        if (err == ESP_OK) {
            s_sda_gpio = sda_gpio;
            s_scl_gpio = scl_gpio;
            s_bus = bus;
        }
    }
    bc250_i2c_service_unlock();
    return err;
}

esp_err_t bc250_i2c_service_add_device(uint8_t address, uint32_t speed_hz,
                                       i2c_master_dev_handle_t *device)
{
    if (device == NULL) return ESP_ERR_INVALID_ARG;
    *device = NULL;
    if (address < 0x08 || address > 0x77 || speed_hz == 0) return ESP_ERR_INVALID_ARG;
    esp_err_t err = bc250_i2c_service_lock();
    if (err != ESP_OK) return err;
    if (s_bus == NULL) {
        err = ESP_ERR_INVALID_STATE;
    } else {
        i2c_device_config_t config = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = address,
            .scl_speed_hz = speed_hz,
            // Allow slow slaves to stretch beyond the driver's 2–2.5 ms default.
            .scl_wait_us = 20000,
        };
        err = i2c_master_bus_add_device(s_bus, &config, device);
    }
    bc250_i2c_service_unlock();
    return err;
}

void bc250_i2c_service_remove_device(i2c_master_dev_handle_t device)
{
    if (device == NULL) return;
    esp_err_t err = bc250_i2c_service_lock();
    if (err == ESP_OK) {
        err = i2c_master_bus_rm_device(device);
        bc250_i2c_service_unlock();
    }
    if (err != ESP_OK) ESP_LOGW(TAG, "Device cleanup failed: %s", esp_err_to_name(err));
}

esp_err_t bc250_i2c_service_recover(void)
{
    return s_bus == NULL ? ESP_ERR_INVALID_STATE : i2c_master_bus_reset(s_bus);
}

static void describe_error(esp_err_t err, int sda_gpio, int scl_gpio, uint8_t address,
                           const char *operation, char *error, size_t error_size)
{
    if (error == NULL || error_size == 0) return;
    const char *reason = err == ESP_ERR_TIMEOUT ? "timed out" :
                         err == ESP_ERR_INVALID_CRC ? "reply checksum failed" : esp_err_to_name(err);
    const char *hint = err == ESP_ERR_TIMEOUT ? "; check ground, wiring and 3.3 V pull-ups" :
                       err == ESP_ERR_INVALID_CRC ? "; check signal quality and PSU compatibility" :
                       err == ESP_ERR_INVALID_RESPONSE || err == ESP_ERR_NOT_FOUND ?
                       "; check PIC address and PSU power" : "";
    snprintf(error, error_size, "I2C %s at 0x%02X %s (SDA GPIO %d=%s, SCL GPIO %d=%s)%s",
             operation, address, reason, sda_gpio, gpio_get_level(sda_gpio) ? "high" : "low",
             scl_gpio, gpio_get_level(scl_gpio) ? "high" : "low", hint);
}

void bc250_i2c_service_describe_error(esp_err_t err, uint8_t address, const char *operation,
                                      char *error, size_t error_size)
{
    describe_error(err, s_sda_gpio, s_scl_gpio, address, operation, error, error_size);
}

esp_err_t bc250_i2c_service_scan(int sda_gpio, int scl_gpio, uint8_t *addresses,
                                size_t capacity, size_t *count, char *error, size_t error_size)
{
    if (error != NULL && error_size > 0) error[0] = '\0';
    if (count != NULL) *count = 0;
    if (addresses == NULL || count == NULL || capacity == 0) return ESP_ERR_INVALID_ARG;
    esp_err_t result = bc250_i2c_service_lock();
    if (result != ESP_OK) {
        if (error != NULL && error_size > 0) snprintf(error, error_size, "I2C bus busy or unavailable; try again");
        return result;
    }
    i2c_master_bus_handle_t bus = s_bus;
    bool temporary = bus == NULL;
    if (temporary) {
        result = new_bus(sda_gpio, scl_gpio, &bus);
    } else if (sda_gpio != s_sda_gpio || scl_gpio != s_scl_gpio) {
        result = ESP_ERR_INVALID_STATE;
    }
    if (result != ESP_OK) {
        bc250_i2c_service_unlock();
        return result;
    }

    for (uint8_t address = 0x08; address <= 0x77; ++address) {
        esp_err_t err = i2c_master_probe(bus, address, 100);
        if (err == ESP_ERR_TIMEOUT) {
            // A scan may follow an interrupted transfer. Clear the bus and retry once.
            describe_error(err, sda_gpio, scl_gpio, address, "scan", error, error_size);
            esp_err_t recovery = i2c_master_bus_reset(bus);
            if (recovery == ESP_OK) err = i2c_master_probe(bus, address, 100);
        }
        if (err == ESP_OK) {
            if (*count == capacity) {
                result = ESP_ERR_INVALID_SIZE;
                break;
            }
            addresses[(*count)++] = address;
        } else if (err != ESP_ERR_NOT_FOUND) {
            result = err;
            // Preserve the original line levels if bus recovery itself failed.
            if (error == NULL || error_size == 0 || error[0] == '\0' || err != ESP_ERR_TIMEOUT)
                describe_error(err, sda_gpio, scl_gpio, address, "scan", error, error_size);
            break;
        }
        if (error != NULL && error_size > 0) error[0] = '\0';
    }
    if (temporary) {
        esp_err_t cleanup = i2c_del_master_bus(bus);
        if (result == ESP_OK) result = cleanup;
    }
    bc250_i2c_service_unlock();
    return result;
}
