#include "zigbee_service.h"

#include <stdint.h>
#include <string.h>

#include "app_events.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_zigbee.h"
#include "ezbee/zha.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "power_service.h"
#include "psu_i2c_service.h"

#define BC250_ZIGBEE_ENDPOINT 1
#define BC250_ZIGBEE_STORAGE_PARTITION "nvs"
#define BC250_ZIGBEE_ALL_CHANNELS 0x07FFF800UL
#define BC250_PSU_REPORT_INTERVAL_US (10LL * 1000 * 1000)

static const char *TAG = "zigbee";
static bc250_config_t s_config;
static uint8_t s_manufacturer[34];
static uint8_t s_model[34];
static volatile bool s_started;
static volatile bool s_joined;
static bool s_internal_attribute_update;
static bool s_psu_was_available;
static int64_t s_psu_last_report_us;

static void update_psu_attribute_cb(void *arg);

static void make_zcl_string(const char *source, uint8_t output[34])
{
    size_t length = strnlen(source, 32);
    output[0] = (uint8_t)length;
    memcpy(&output[1], source, length);
}

static void commission_cb(void *arg)
{
    (void)arg;
    ezb_bdb_start_top_level_commissioning(EZB_BDB_MODE_NETWORK_STEERING);
}

static bool signal_handler(const ezb_app_signal_t *signal)
{
    ezb_app_signal_type_t type = ezb_app_signal_get_type(signal);
    switch (type) {
    case EZB_ZDO_SIGNAL_SKIP_STARTUP:
        ezb_bdb_start_top_level_commissioning(EZB_BDB_MODE_INITIALIZATION);
        break;
    case EZB_BDB_SIGNAL_DEVICE_FIRST_START:
    case EZB_BDB_SIGNAL_DEVICE_REBOOT: {
        ezb_bdb_comm_status_t status = *(ezb_bdb_comm_status_t *)ezb_app_signal_get_params(signal);
        if (status == EZB_BDB_STATUS_SUCCESS) {
            s_started = true;
            if (ezb_bdb_is_factory_new()) commission_cb(NULL);
            else {
                s_joined = true;
                s_psu_last_report_us = 0;
            }
            bc250_zigbee_update_power_state(BC250_POWER_UNKNOWN);
            bc250_zigbee_update_psu_status();
        }
        break;
    }
    case EZB_BDB_SIGNAL_STEERING: {
        ezb_bdb_comm_status_t status = *(ezb_bdb_comm_status_t *)ezb_app_signal_get_params(signal);
        s_joined = status == EZB_BDB_STATUS_SUCCESS;
        s_psu_last_report_us = 0;
        ESP_LOGI(TAG, "Network steering %s", s_joined ? "complete" : "failed");
        if (s_joined) bc250_zigbee_update_psu_status();
        break;
    }
    case EZB_ZDO_SIGNAL_LEAVE:
        s_joined = false;
        s_psu_last_report_us = 0;
        break;
    default:
        break;
    }
    return true;
}

static void zcl_handler(ezb_zcl_core_action_callback_id_t callback_id, void *message)
{
    if (callback_id != EZB_ZCL_CORE_SET_ATTR_VALUE_CB_ID || s_internal_attribute_update) return;
    ezb_zcl_set_attr_value_message_t *set = message;
    if (set == NULL || set->info.cluster_id != EZB_ZCL_CLUSTER_ID_ON_OFF ||
        set->in.attribute.id != EZB_ZCL_ATTR_ON_OFF_ON_OFF_ID || set->in.attribute.data.value == NULL) {
        return;
    }
    bool requested_on = *(bool *)set->in.attribute.data.value;
    /* Local attribute refreshes mirror the sense input and are not commands. */
    if (requested_on == bc250_power_service_sensed_on()) return;
    bc250_app_event_t event = {
        .type = BC250_EVENT_BUTTON_ACTION,
        .data.button_action = requested_on ? BC250_BUTTON_ACTION_ON : BC250_BUTTON_ACTION_OFF,
    };
    bc250_app_event_post(&event, 0);
}

static void add_psu_clusters(ezb_af_ep_desc_t endpoint)
{
    ezb_zcl_electrical_measurement_cluster_server_config_t config = {
        .measurement_type = EZB_ZCL_ELECTRICAL_MEASUREMENT_MEASUREMENT_TYPE_ACTIVE_MEASUREMENT_AC |
                            EZB_ZCL_ELECTRICAL_MEASUREMENT_MEASUREMENT_TYPE_DC_MEASUREMENT,
    };
    ezb_zcl_cluster_desc_t electrical = ezb_zcl_electrical_measurement_create_cluster_desc(
        &config, EZB_ZCL_CLUSTER_SERVER);
    ESP_ERROR_CHECK(electrical == EZB_INVALID_ZCL_CLUSTER_DESC ? ESP_ERR_NO_MEM : ESP_OK);

    uint16_t one = 1;
    uint16_t ten = 10;
    uint16_t hundred = 100;
    uint16_t ac_unknown = UINT16_MAX;
    int16_t dc_unknown = INT16_MIN;
#define ADD_ELECTRICAL(id, value) ESP_ERROR_CHECK(ezb_zcl_electrical_measurement_cluster_desc_add_attr( \
    electrical, EZB_ZCL_ATTR_ELECTRICAL_MEASUREMENT_##id##_ID, &(value)))
    ADD_ELECTRICAL(RMS_VOLTAGE, ac_unknown);
    ADD_ELECTRICAL(RMS_CURRENT, ac_unknown);
    ADD_ELECTRICAL(DC_VOLTAGE, dc_unknown);
    ADD_ELECTRICAL(DC_CURRENT, dc_unknown);
    ADD_ELECTRICAL(AC_VOLTAGE_MULTIPLIER, one);
    ADD_ELECTRICAL(AC_VOLTAGE_DIVISOR, ten);
    ADD_ELECTRICAL(AC_CURRENT_MULTIPLIER, one);
    ADD_ELECTRICAL(AC_CURRENT_DIVISOR, hundred);
    ADD_ELECTRICAL(DC_VOLTAGE_MULTIPLIER, one);
    ADD_ELECTRICAL(DC_VOLTAGE_DIVISOR, hundred);
    ADD_ELECTRICAL(DC_CURRENT_MULTIPLIER, one);
    ADD_ELECTRICAL(DC_CURRENT_DIVISOR, ten);
#undef ADD_ELECTRICAL
    ESP_ERROR_CHECK(ezb_af_endpoint_add_cluster_desc(endpoint, electrical));

    ezb_zcl_analog_input_cluster_server_config_t fan_config = {
        .present_value = 0,
        .status_flags = EZB_ZCL_ANALOG_INPUT_STATUS_FLAGS_FAULT,
    };
    ezb_zcl_cluster_desc_t fan = ezb_zcl_analog_input_create_cluster_desc(
        &fan_config, EZB_ZCL_CLUSTER_SERVER);
    ESP_ERROR_CHECK(fan == EZB_INVALID_ZCL_CLUSTER_DESC ? ESP_ERR_NO_MEM : ESP_OK);
    static uint8_t fan_description[] = "\x0b" "PSU fan raw";
    ESP_ERROR_CHECK(ezb_zcl_analog_input_cluster_desc_add_attr(
        fan, EZB_ZCL_ATTR_ANALOG_INPUT_DESCRIPTION_ID, fan_description));
    ESP_ERROR_CHECK(ezb_af_endpoint_add_cluster_desc(endpoint, fan));
}

static esp_err_t create_device(void)
{
    make_zcl_string(s_config.zigbee_manufacturer, s_manufacturer);
    make_zcl_string(s_config.zigbee_model, s_model);
    ezb_af_device_desc_t device = ezb_af_create_device_desc();
    ezb_zha_on_off_light_config_t light = EZB_ZHA_ON_OFF_LIGHT_CONFIG();
    ezb_af_ep_desc_t endpoint = ezb_zha_create_on_off_light(BC250_ZIGBEE_ENDPOINT, &light);
    /* Keep the server clusters, but identify the endpoint as a controllable output. */
    ESP_ERROR_CHECK(ezb_af_ep_desc_set_app_device_id(endpoint, EZB_ZHA_ON_OFF_OUTPUT_DEVICE_ID));
    ezb_zcl_cluster_desc_t basic = ezb_af_endpoint_get_cluster_desc(
        endpoint, EZB_ZCL_CLUSTER_ID_BASIC, EZB_ZCL_CLUSTER_SERVER);
    ezb_zcl_basic_cluster_desc_add_attr(basic, EZB_ZCL_ATTR_BASIC_MANUFACTURER_NAME_ID,
                                        s_manufacturer);
    ezb_zcl_basic_cluster_desc_add_attr(basic, EZB_ZCL_ATTR_BASIC_MODEL_IDENTIFIER_ID,
                                        s_model);
    if (s_config.psu_i2c.enabled) add_psu_clusters(endpoint);
    ESP_ERROR_CHECK(ezb_af_device_add_endpoint_desc(device, endpoint));
    ESP_ERROR_CHECK(ezb_af_device_desc_register(device));
    ezb_zcl_core_action_handler_register(zcl_handler);
    return ESP_OK;
}

static void zigbee_task(void *arg)
{
    (void)arg;
    esp_zigbee_config_t config = {
        .device_config = {
            .device_type = EZB_NWK_DEVICE_TYPE_ROUTER,
            .install_code_policy = false,
            .zczr_config = {.max_children = 10},
        },
        .platform_config = {
            .storage_partition_name = BC250_ZIGBEE_STORAGE_PARTITION,
            .radio_config = {.radio_mode = ESP_ZIGBEE_RADIO_MODE_NATIVE},
        },
    };
    ESP_ERROR_CHECK(esp_zigbee_init(&config));
    uint32_t channel_mask = s_config.zigbee_channel >= 11 && s_config.zigbee_channel <= 26
                                ? 1UL << s_config.zigbee_channel
                                : BC250_ZIGBEE_ALL_CHANNELS;
    ezb_aps_secur_enable_distributed_security(false);
    ESP_ERROR_CHECK(ezb_bdb_set_primary_channel_set(channel_mask));
    ESP_ERROR_CHECK(ezb_bdb_set_secondary_channel_set(BC250_ZIGBEE_ALL_CHANNELS));
    ESP_ERROR_CHECK(ezb_app_signal_add_handler(signal_handler));
    ESP_ERROR_CHECK(create_device());
    ESP_ERROR_CHECK(esp_zigbee_start(false));
    esp_zigbee_launch_mainloop();
    esp_zigbee_deinit();
    s_started = false;
    s_joined = false;
    vTaskDelete(NULL);
}

esp_err_t bc250_zigbee_service_start(const bc250_config_t *config)
{
    if (config == NULL) return ESP_ERR_INVALID_ARG;
    if (config->radio_profile == BC250_RADIO_WIFI) return ESP_OK;
    s_config = *config;
    return xTaskCreate(zigbee_task, "Zigbee_main", 6144, NULL, 5, NULL) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t bc250_zigbee_commission(void)
{
    return s_started ? esp_zigbee_task_queue_post(commission_cb, NULL) : ESP_ERR_INVALID_STATE;
}

static void reset_cb(void *arg)
{
    (void)arg;
    esp_zigbee_factory_reset();
}

esp_err_t bc250_zigbee_factory_reset(void)
{
    return s_started ? esp_zigbee_task_queue_post(reset_cb, NULL) : ESP_ERR_INVALID_STATE;
}

static void update_attribute_cb(void *arg)
{
    bool on = (bool)(uintptr_t)arg;
    s_internal_attribute_update = true;
    ezb_zcl_set_attr_value(BC250_ZIGBEE_ENDPOINT, EZB_ZCL_CLUSTER_ID_ON_OFF,
                           EZB_ZCL_CLUSTER_SERVER, EZB_ZCL_ATTR_ON_OFF_ON_OFF_ID,
                           EZB_ZCL_STD_MANUF_CODE, &on, false);
    s_internal_attribute_update = false;
}

void bc250_zigbee_update_power_state(bc250_power_state_t state)
{
    (void)state;
    if (!s_started) return;
    bool on = bc250_power_service_sensed_on();
    esp_zigbee_task_queue_post(update_attribute_cb, (void *)(uintptr_t)on);
}

static bool set_psu_attr(uint16_t cluster_id, uint16_t attr_id, void *value)
{
    ezb_zcl_status_t status = ezb_zcl_set_attr_value(
        BC250_ZIGBEE_ENDPOINT, cluster_id, EZB_ZCL_CLUSTER_SERVER, attr_id,
        EZB_ZCL_STD_MANUF_CODE, value, false);
    if (status != EZB_ZCL_STATUS_SUCCESS) {
        ESP_LOGW(TAG, "PSU attribute 0x%04x/0x%04x update failed: 0x%02x",
                 cluster_id, attr_id, status);
        return false;
    }
    return true;
}

static bool report_psu_attr(uint16_t cluster_id, uint16_t attr_id)
{
    ezb_zcl_report_attr_cmd_t report = {
        .cmd_ctrl = {
            .dst_addr = {.addr_mode = EZB_ADDR_MODE_SHORT, .u.short_addr = 0x0000},
            .dst_ep = 1,
            .src_ep = BC250_ZIGBEE_ENDPOINT,
            .cluster_id = cluster_id,
            .fc.direction = EZB_ZCL_CMD_DIRECTION_TO_CLI,
        },
        .payload.attr_id = attr_id,
    };
    ezb_err_t err = ezb_zcl_report_attr_cmd_req(&report);
    if (err != EZB_ERR_NONE) {
        ESP_LOGW(TAG, "PSU attribute 0x%04x/0x%04x report failed: %d",
                 cluster_id, attr_id, err);
        return false;
    }
    return true;
}

static void update_psu_attribute_cb(void *arg)
{
    (void)arg;
    bc250_psu_i2c_status_t psu = bc250_psu_i2c_service_status();
    if (!psu.enabled) return;

    const uint16_t electrical = EZB_ZCL_CLUSTER_ID_ELECTRICAL_MEASUREMENT;
    const uint16_t analog = EZB_ZCL_CLUSTER_ID_ANALOG_INPUT;
    uint16_t input_voltage = psu.available ? (uint16_t)(psu.input_voltage_v * 10.0f + 0.5f) : UINT16_MAX;
    uint16_t input_current = psu.available ? (uint16_t)(psu.input_current_a * 100.0f + 0.5f) : UINT16_MAX;
    int16_t output_voltage = psu.available ? (int16_t)(psu.output_voltage_v * 100.0f + 0.5f) : INT16_MIN;
    int16_t output_current = psu.available ? (int16_t)(psu.output_current_a * 10.0f + 0.5f) : INT16_MIN;
    float fan = psu.available ? (float)psu.fan_speed_raw : 0.0f;
    uint8_t fan_flags = psu.available ? 0 : EZB_ZCL_ANALOG_INPUT_STATUS_FLAGS_FAULT;

    bool updated = true;
    updated &= set_psu_attr(electrical, EZB_ZCL_ATTR_ELECTRICAL_MEASUREMENT_RMS_VOLTAGE_ID, &input_voltage);
    updated &= set_psu_attr(electrical, EZB_ZCL_ATTR_ELECTRICAL_MEASUREMENT_RMS_CURRENT_ID, &input_current);
    updated &= set_psu_attr(electrical, EZB_ZCL_ATTR_ELECTRICAL_MEASUREMENT_DC_VOLTAGE_ID, &output_voltage);
    updated &= set_psu_attr(electrical, EZB_ZCL_ATTR_ELECTRICAL_MEASUREMENT_DC_CURRENT_ID, &output_current);
    updated &= set_psu_attr(analog, EZB_ZCL_ATTR_ANALOG_INPUT_PRESENT_VALUE_ID, &fan);
    updated &= set_psu_attr(analog, EZB_ZCL_ATTR_ANALOG_INPUT_STATUS_FLAGS_ID, &fan_flags);
    if (!updated) return;

    bool availability_changed = psu.available != s_psu_was_available;
    s_psu_was_available = psu.available;
    if (!s_joined) return;

    int64_t now = esp_timer_get_time();
    if (!availability_changed && s_psu_last_report_us != 0 &&
        (!psu.available || now - s_psu_last_report_us < BC250_PSU_REPORT_INTERVAL_US)) return;

    bool reported = true;
    reported &= report_psu_attr(electrical, EZB_ZCL_ATTR_ELECTRICAL_MEASUREMENT_RMS_VOLTAGE_ID);
    reported &= report_psu_attr(electrical, EZB_ZCL_ATTR_ELECTRICAL_MEASUREMENT_RMS_CURRENT_ID);
    reported &= report_psu_attr(electrical, EZB_ZCL_ATTR_ELECTRICAL_MEASUREMENT_DC_VOLTAGE_ID);
    reported &= report_psu_attr(electrical, EZB_ZCL_ATTR_ELECTRICAL_MEASUREMENT_DC_CURRENT_ID);
    if (psu.available) reported &= report_psu_attr(analog, EZB_ZCL_ATTR_ANALOG_INPUT_PRESENT_VALUE_ID);
    if (availability_changed || s_psu_last_report_us == 0) {
        reported &= report_psu_attr(analog, EZB_ZCL_ATTR_ANALOG_INPUT_STATUS_FLAGS_ID);
    }
    if (reported) s_psu_last_report_us = now;
}

void bc250_zigbee_update_psu_status(void)
{
    if (!s_started || !s_config.psu_i2c.enabled) return;
    esp_zigbee_task_queue_post(update_psu_attribute_cb, NULL);
}

bool bc250_zigbee_is_started(void)
{
    return s_started;
}

bool bc250_zigbee_is_joined(void)
{
    return s_joined;
}
