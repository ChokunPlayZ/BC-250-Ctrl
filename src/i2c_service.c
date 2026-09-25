#include "i2c_service.h"

#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static i2c_master_bus_handle_t s_bus;
static SemaphoreHandle_t s_mutex;
static int s_sda_gpio = -1;
static int s_scl_gpio = -1;

static esp_err_t new_bus(int sda_gpio, int scl_gpio, i2c_master_bus_handle_t *bus)
{
    i2c_master_bus_config_t config = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = sda_gpio,
        .scl_io_num = scl_gpio,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = false,
    };
    return i2c_new_master_bus(&config, bus);
}

esp_err_t bc250_i2c_service_start(int sda_gpio, int scl_gpio)
{
    if (s_bus != NULL) {
        return sda_gpio == s_sda_gpio && scl_gpio == s_scl_gpio ? ESP_OK : ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = new_bus(sda_gpio, scl_gpio, &s_bus);
    if (err != ESP_OK) return err;
    s_mutex = xSemaphoreCreateMutex();
    if (s_mutex == NULL) {
        i2c_del_master_bus(s_bus);
        s_bus = NULL;
        return ESP_ERR_NO_MEM;
    }
    s_sda_gpio = sda_gpio;
    s_scl_gpio = scl_gpio;
    return ESP_OK;
}

esp_err_t bc250_i2c_service_add_device(uint8_t address, uint32_t speed_hz,
                                       i2c_master_dev_handle_t *device)
{
    if (s_bus == NULL || device == NULL) return ESP_ERR_INVALID_STATE;
    *device = NULL;
    i2c_device_config_t config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = address,
        .scl_speed_hz = speed_hz,
    };
    bc250_i2c_service_lock();
    esp_err_t err = i2c_master_bus_add_device(s_bus, &config, device);
    bc250_i2c_service_unlock();
    return err;
}

void bc250_i2c_service_remove_device(i2c_master_dev_handle_t device)
{
    if (device == NULL) return;
    bc250_i2c_service_lock();
    i2c_master_bus_rm_device(device);
    bc250_i2c_service_unlock();
}

void bc250_i2c_service_lock(void)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
}

void bc250_i2c_service_unlock(void)
{
    xSemaphoreGive(s_mutex);
}

esp_err_t bc250_i2c_service_scan(int sda_gpio, int scl_gpio, uint8_t *addresses,
                                size_t capacity, size_t *count)
{
    if (addresses == NULL || count == NULL || capacity == 0) return ESP_ERR_INVALID_ARG;
    *count = 0;

    i2c_master_bus_handle_t bus = s_bus;
    bool temporary = bus == NULL;
    if (temporary) {
        esp_err_t err = new_bus(sda_gpio, scl_gpio, &bus);
        if (err != ESP_OK) return err;
    } else {
        if (sda_gpio != s_sda_gpio || scl_gpio != s_scl_gpio) return ESP_ERR_INVALID_STATE;
        bc250_i2c_service_lock();
    }

    esp_err_t result = ESP_OK;
    for (uint8_t address = 0x08; address <= 0x77; ++address) {
        esp_err_t err = i2c_master_probe(bus, address, 50);
        if (err == ESP_OK) {
            if (*count == capacity) {
                result = ESP_ERR_INVALID_SIZE;
                break;
            }
            addresses[(*count)++] = address;
        } else if (err != ESP_ERR_NOT_FOUND) {
            result = err;
            break;
        }
    }
    if (temporary) {
        esp_err_t cleanup = i2c_del_master_bus(bus);
        if (result == ESP_OK) result = cleanup;
    } else {
        bc250_i2c_service_unlock();
    }
    return result;
}
