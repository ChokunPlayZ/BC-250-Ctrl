#include "psu_i2c_service.h"

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
static bc250_psu_i2c_status_t s_status;
static int64_t s_sample_us;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

static esp_err_t read_register(uint8_t reg, uint16_t *raw)
{
    bc250_i2c_service_lock();
    uint8_t command[2];
    uint8_t reply[3];
    bc250_hp_commonslot_read_command(s_config.address, reg, command);
    esp_err_t err = i2c_master_transmit(s_device, command, sizeof(command), 100);
    if (err == ESP_OK) {
        // The reference sketch uses separate transactions with a short pause.
        vTaskDelay(pdMS_TO_TICKS(1) > 0 ? pdMS_TO_TICKS(1) : 1);
        err = i2c_master_receive(s_device, reply, sizeof(reply), 100);
        if (err == ESP_OK && !bc250_hp_commonslot_decode_reply(reply, raw)) {
            err = ESP_ERR_INVALID_CRC;
        }
    }
    bc250_i2c_service_unlock();
    return err;
}

static void psu_task(void *arg)
{
    (void)arg;
    bool failure_logged = false;
    while (true) {
        uint16_t raw[BC250_HP_COMMONSLOT_REGISTER_COUNT];
        esp_err_t err = ESP_OK;
        for (unsigned i = 0; i < BC250_HP_COMMONSLOT_REGISTER_COUNT; ++i) {
            err = read_register(bc250_hp_commonslot_registers[i], &raw[i]);
            if (err != ESP_OK) break;
        }
        portENTER_CRITICAL(&s_lock);
        s_status.available = err == ESP_OK;
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
        bc250_zigbee_update_psu_status();
        if (err != ESP_OK && !failure_logged) {
            ESP_LOGW(TAG, "PSU telemetry unavailable: %s", esp_err_to_name(err));
            failure_logged = true;
        } else if (err == ESP_OK && failure_logged) {
            ESP_LOGI(TAG, "PSU telemetry restored");
            failure_logged = false;
        }
        vTaskDelay(pdMS_TO_TICKS(s_config.poll_interval_ms));
    }
}

esp_err_t bc250_psu_i2c_service_start(const bc250_config_t *config)
{
    if (config == NULL) return ESP_ERR_INVALID_ARG;
    s_config = config->psu_i2c;
    s_status.enabled = s_config.enabled;
    if (!s_config.enabled) return ESP_OK;

    esp_err_t err = bc250_i2c_service_start(s_config.sda_gpio, s_config.scl_gpio);
    if (err != ESP_OK) return err;
    err = bc250_i2c_service_add_device(s_config.address, 100000, &s_device);
    if (err != ESP_OK) return err;
    if (xTaskCreate(psu_task, "psu_i2c", 3072, NULL, 5, NULL) != pdPASS) {
        bc250_i2c_service_remove_device(s_device);
        s_device = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
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
