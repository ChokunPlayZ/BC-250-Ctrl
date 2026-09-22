#include "zigbee_service.h"

#include <string.h>

#include "app_events.h"
#include "esp_log.h"
#include "esp_zigbee.h"
#include "ezbee/zha.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define BC250_ZIGBEE_ENDPOINT 1
#define BC250_ZIGBEE_STORAGE_PARTITION "nvs"
#define BC250_ZIGBEE_ALL_CHANNELS 0x07FFF800UL

static const char *TAG = "zigbee";
static bc250_config_t s_config;
static uint8_t s_manufacturer[34];
static uint8_t s_model[34];
static volatile bool s_started;
static volatile bool s_joined;
static bool s_internal_attribute_update;

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
            else s_joined = true;
        }
        break;
    }
    case EZB_BDB_SIGNAL_STEERING: {
        ezb_bdb_comm_status_t status = *(ezb_bdb_comm_status_t *)ezb_app_signal_get_params(signal);
        s_joined = status == EZB_BDB_STATUS_SUCCESS;
        ESP_LOGI(TAG, "Network steering %s", s_joined ? "complete" : "failed");
        break;
    }
    case EZB_ZDO_SIGNAL_LEAVE:
        s_joined = false;
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
    bc250_app_event_t event = {
        .type = BC250_EVENT_BUTTON_ACTION,
        .data.button_action = requested_on ? BC250_BUTTON_ACTION_ON : BC250_BUTTON_ACTION_OFF,
    };
    bc250_app_event_post(&event, 0);
}

static esp_err_t create_device(void)
{
    make_zcl_string(s_config.zigbee_manufacturer, s_manufacturer);
    make_zcl_string(s_config.zigbee_model, s_model);
    ezb_af_device_desc_t device = ezb_af_create_device_desc();
    ezb_zha_on_off_light_config_t light = EZB_ZHA_ON_OFF_LIGHT_CONFIG();
    ezb_af_ep_desc_t endpoint = ezb_zha_create_on_off_light(BC250_ZIGBEE_ENDPOINT, &light);
    ezb_zcl_cluster_desc_t basic = ezb_af_endpoint_get_cluster_desc(
        endpoint, EZB_ZCL_CLUSTER_ID_BASIC, EZB_ZCL_CLUSTER_SERVER);
    ezb_zcl_basic_cluster_desc_add_attr(basic, EZB_ZCL_ATTR_BASIC_MANUFACTURER_NAME_ID,
                                        s_manufacturer);
    ezb_zcl_basic_cluster_desc_add_attr(basic, EZB_ZCL_ATTR_BASIC_MODEL_IDENTIFIER_ID,
                                        s_model);
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
    if (!s_started) return;
    bool on = state == BC250_POWER_ON || state == BC250_POWER_STARTING;
    esp_zigbee_task_queue_post(update_attribute_cb, (void *)(uintptr_t)on);
}

bool bc250_zigbee_is_started(void)
{
    return s_started;
}

bool bc250_zigbee_is_joined(void)
{
    return s_joined;
}
