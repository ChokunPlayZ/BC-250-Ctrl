#include "esp_log.h"
#include "esp_partition.h"
#include "nvs_flash.h"

static const char *TAG = "recovery";

void app_main(void)
{
    ESP_LOGW(TAG, "BC250 recovery: erasing all saved settings");
    /* Do not initialize controller services or load any saved GPIO settings. */
    esp_err_t err = nvs_flash_erase();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS erase failed: %s", esp_err_to_name(err));
        return;
    }
    const esp_partition_t *factory = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "zb_fct");
    if (factory == NULL) {
        ESP_LOGE(TAG, "Zigbee factory partition not found; recovery incomplete");
        return;
    }
    err = esp_partition_erase_range(factory, 0, factory->size);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Zigbee factory erase failed: %s", esp_err_to_name(err));
        return;
    }
    ESP_LOGI(TAG, "RECOVERY COMPLETE: configuration, credentials and Zigbee state erased");
    ESP_LOGI(TAG, "Flash the normal BC250 firmware now. This image has no setup AP.");
    /* Stay idle; rebooting this image simply repeats the erase. */
}
