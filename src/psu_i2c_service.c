#include "psu_i2c_service.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "core/hp_commonslot_protocol.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "i2c_service.h"
#include "zigbee_service.h"

static const char *TAG = "psu_i2c";
static bc250_psu_i2c_config_t s_config;
static i2c_master_dev_handle_t s_device;
static i2c_master_dev_handle_t s_eeprom_device;
static bc250_psu_i2c_status_t s_status;
static int64_t s_sample_us;
static bool s_started;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

static void read_identity(void)
{
    bc250_psu_i2c_identity_status_t identity = {.eeprom_address = (uint8_t)(s_config.address - 8)};
    esp_err_t err = ESP_OK;
    if (s_eeprom_device == NULL)
        err = bc250_i2c_service_add_device(identity.eeprom_address, 100000, &s_eeprom_device);
    if (err == ESP_OK) err = bc250_i2c_service_lock();
    if (err == ESP_OK) {
        if (!bc250_i2c_service_clock_high()) {
            bc250_i2c_service_describe_error(ESP_ERR_TIMEOUT, identity.eeprom_address,
                                             "EEPROM read skipped (clock held low)",
                                             identity.error, sizeof(identity.error));
        } else {
            uint8_t eeprom[BC250_HP_EEPROM_SIZE];
            for (unsigned attempt = 0; attempt < 2; ++attempt) {
                for (unsigned offset = 0; offset < sizeof(eeprom); offset += 32) {
                    uint8_t pointer = (uint8_t)offset;
                    // A one-byte EEPROM address selects the read pointer; no EEPROM contents are written.
                    err = i2c_master_transmit_receive(s_eeprom_device, &pointer, 1, eeprom + offset, 32, 100);
                    if (err != ESP_OK) break;
                }
                if (err == ESP_OK) break;
                bc250_i2c_service_describe_error(err, identity.eeprom_address, "EEPROM read",
                                                 identity.error, sizeof(identity.error));
                if (err != ESP_ERR_TIMEOUT || attempt != 0 || !bc250_i2c_service_clock_high() ||
                    bc250_i2c_service_recover() != ESP_OK) break;
            }
            if (err == ESP_OK) {
                identity.error[0] = '\0';
                identity.available = bc250_hp_commonslot_decode_identity(eeprom, sizeof(eeprom), &identity.data);
                if (!identity.available) snprintf(identity.error, sizeof(identity.error),
                                                   "EEPROM at 0x%02X has unsupported or corrupt FRU identification",
                                                   identity.eeprom_address);
            }
        }
        bc250_i2c_service_unlock();
    } else {
        snprintf(identity.error, sizeof(identity.error), "PSU identification unavailable: %s", esp_err_to_name(err));
    }
    portENTER_CRITICAL(&s_lock);
    s_status.identity = identity;
    portEXIT_CRITICAL(&s_lock);
}

static esp_err_t read_register(uint8_t reg, uint16_t *raw, char error[BC250_I2C_ERROR_SIZE])
{
    esp_err_t err = bc250_i2c_service_lock();
    if (err != ESP_OK) {
        snprintf(error, BC250_I2C_ERROR_SIZE, "I2C bus busy or unavailable; retrying on next poll (%s)",
                 esp_err_to_name(err));
        return err;
    }
    if (!bc250_i2c_service_clock_high()) {
        char operation[48];
        snprintf(operation, sizeof(operation), "write register 0x%02X skipped (clock held low)", reg);
        bc250_i2c_service_describe_error(ESP_ERR_TIMEOUT, s_config.address,
                                         operation, error, BC250_I2C_ERROR_SIZE);
        bc250_i2c_service_unlock();
        return ESP_ERR_TIMEOUT;
    }
    uint8_t command[2];
    uint8_t reply[3];
    bc250_hp_commonslot_read_command(s_config.address, reg, command);
    for (unsigned attempt = 0; attempt < 2; ++attempt) {
        const char *phase = "write";
        err = i2c_master_transmit(s_device, command, sizeof(command), 100);
        if (err == ESP_OK) {
            // The reference sketch uses separate transactions with a short pause.
            vTaskDelay(pdMS_TO_TICKS(1) > 0 ? pdMS_TO_TICKS(1) : 1);
            phase = "read";
            err = i2c_master_receive(s_device, reply, sizeof(reply), 100);
            if (err == ESP_OK && !bc250_hp_commonslot_decode_reply(reply, raw)) {
                err = ESP_ERR_INVALID_CRC;
            }
        }
        if (err == ESP_OK) {
            error[0] = '\0';
            break;
        }
        char operation[32];
        snprintf(operation, sizeof(operation), "%s register 0x%02X", phase, reg);
        bc250_i2c_service_describe_error(err, s_config.address, operation, error, BC250_I2C_ERROR_SIZE);
        if (err != ESP_ERR_TIMEOUT || attempt != 0 || !bc250_i2c_service_clock_high() ||
            bc250_i2c_service_recover() != ESP_OK) break;
        // Restart the whole command/read pair after clearing an interrupted transfer.
    }
    bc250_i2c_service_unlock();
    return err;
}

static void psu_task(void *arg)
{
    (void)arg;
    bool failure_logged = false;
    bool sampled = false;
    int64_t next_identity_us = 0;
    while (true) {
        uint16_t raw[BC250_HP_COMMONSLOT_REGISTER_COUNT];
        char error[BC250_I2C_ERROR_SIZE] = "";
        esp_err_t err = ESP_OK;
        for (unsigned i = 0; i < BC250_HP_COMMONSLOT_REGISTER_COUNT; ++i) {
            err = read_register(bc250_hp_commonslot_registers[i], &raw[i], error);
            if (err != ESP_OK) break;
        }
        portENTER_CRITICAL(&s_lock);
        s_status.available = err == ESP_OK;
        memcpy(s_status.error, error, sizeof(s_status.error));
        if (err == ESP_OK) {
            s_status.input_voltage_v = bc250_hp_commonslot_scale(0, raw[0]);
            s_status.input_current_a = bc250_hp_commonslot_scale(1, raw[1]);
            s_status.output_voltage_v = bc250_hp_commonslot_scale(2, raw[2]);
            s_status.output_current_a = bc250_hp_commonslot_scale(3, raw[3]);
            s_status.internal_temperature_f = bc250_hp_commonslot_scale(4, raw[4]);
            s_status.fan_speed_raw = raw[5];
            s_sample_us = esp_timer_get_time();
        }
        portEXIT_CRITICAL(&s_lock);
        // Identification is optional and uses its own error/availability state.
        int64_t now = esp_timer_get_time();
        if (now >= next_identity_us) {
            read_identity();
            next_identity_us = esp_timer_get_time() + 60000000;
        }
        bc250_zigbee_update_psu_status();
        if (err != ESP_OK && !failure_logged) {
            ESP_LOGW(TAG, "PSU telemetry unavailable: %s", error);
            failure_logged = true;
        } else if (err == ESP_OK && (!sampled || failure_logged)) {
            ESP_LOGI(TAG, "PSU telemetry %s (address=0x%02x)", failure_logged ? "restored" : "available", s_config.address);
            failure_logged = false;
        }
        if (err == ESP_OK) sampled = true;
        vTaskDelay(pdMS_TO_TICKS(s_config.poll_interval_ms));
    }
}

esp_err_t bc250_psu_i2c_service_start(const bc250_config_t *config)
{
    if (config == NULL) return ESP_ERR_INVALID_ARG;
    if (s_started) return ESP_ERR_INVALID_STATE;
    s_config = config->psu_i2c;
    portENTER_CRITICAL(&s_lock);
    memset(&s_status, 0, sizeof(s_status));
    s_status.enabled = s_config.enabled;
    s_status.identity.eeprom_address = (uint8_t)(s_config.address - 8);
    s_sample_us = 0;
    portEXIT_CRITICAL(&s_lock);
    if (!s_config.enabled) return ESP_OK;
    ESP_LOGI(TAG, "Starting PSU telemetry: SDA=%d; SCL=%d; address=0x%02x; poll=%" PRIu32 " ms",
             s_config.sda_gpio, s_config.scl_gpio, s_config.address, s_config.poll_interval_ms);

    char error[BC250_I2C_ERROR_SIZE] = "";
    esp_err_t err = bc250_config_validate_i2c_pins(config, s_config.sda_gpio, s_config.scl_gpio,
                                                 error, sizeof(error));
    if (err != ESP_OK) goto failed;
    if (s_config.address < 0x58 || s_config.address > 0x5f ||
        s_config.poll_interval_ms < 500 || s_config.poll_interval_ms > 60000) {
        err = ESP_ERR_INVALID_ARG;
        snprintf(error, sizeof(error), "Invalid PSU PIC address or polling interval");
        goto failed;
    }
    err = bc250_i2c_service_start(s_config.sda_gpio, s_config.scl_gpio);
    if (err != ESP_OK) goto failed;
    err = bc250_i2c_service_add_device(s_config.address, 100000, &s_device);
    if (err != ESP_OK) goto failed;
    if (xTaskCreate(psu_task, "psu_i2c", 4096, NULL, 5, NULL) != pdPASS) {
        bc250_i2c_service_remove_device(s_device);
        s_device = NULL;
        err = ESP_ERR_NO_MEM;
        goto failed;
    }
    s_started = true;
    return ESP_OK;

failed:
    if (!error[0]) snprintf(error, sizeof(error), "I2C startup failed (SDA GPIO %d, SCL GPIO %d, PIC 0x%02X): %s",
                            s_config.sda_gpio, s_config.scl_gpio, s_config.address, esp_err_to_name(err));
    portENTER_CRITICAL(&s_lock);
    memcpy(s_status.error, error, sizeof(s_status.error));
    portEXIT_CRITICAL(&s_lock);
    ESP_LOGW(TAG, "%s", error);
    return err;
}

bc250_psu_i2c_status_t bc250_psu_i2c_service_status(void)
{
    portENTER_CRITICAL(&s_lock);
    bc250_psu_i2c_status_t status = s_status;
    int64_t sampled = s_sample_us;
    portEXIT_CRITICAL(&s_lock);
    status.age_ms = sampled > 0 ? (uint32_t)((esp_timer_get_time() - sampled) / 1000) : 0;
    return status;
}
