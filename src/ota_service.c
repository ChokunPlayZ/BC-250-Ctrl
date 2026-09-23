#include "ota_service.h"

#include "esp_app_format.h"
#include "esp_log.h"
#include "esp_ota_ops.h"

#if CONFIG_BC250_OTA_ENABLED
static const char *TAG = "ota";
#endif

esp_err_t bc250_ota_handle_http(httpd_req_t *request)
{
#if !CONFIG_BC250_OTA_ENABLED
    httpd_resp_send_err(request, HTTPD_404_NOT_FOUND, "OTA requires the 8 MB firmware build");
    return ESP_ERR_NOT_SUPPORTED;
#else
    const esp_partition_t *target = esp_ota_get_next_update_partition(NULL);
    if (target == NULL) {
        httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "No OTA partition");
        return ESP_FAIL;
    }
    esp_ota_handle_t handle;
    esp_err_t err = esp_ota_begin(target, request->content_len, &handle);
    if (err != ESP_OK) {
        httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA begin failed");
        return err;
    }
    uint8_t buffer[2048];
    int remaining = request->content_len;
    while (remaining > 0) {
        int received = httpd_req_recv(request, (char *)buffer,
                                      remaining < (int)sizeof(buffer) ? remaining : sizeof(buffer));
        if (received == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (received <= 0) {
            esp_ota_abort(handle);
            httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA receive failed");
            return ESP_FAIL;
        }
        err = esp_ota_write(handle, buffer, received);
        if (err != ESP_OK) {
            esp_ota_abort(handle);
            httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA write failed");
            return err;
        }
        remaining -= received;
    }
    err = esp_ota_end(handle);
    if (err == ESP_OK) err = esp_ota_set_boot_partition(target);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OTA image rejected: %s", esp_err_to_name(err));
        httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Firmware image rejected");
        return err;
    }
    httpd_resp_set_type(request, "application/json");
    httpd_resp_sendstr(request, "{\"accepted\":true,\"reboot_required\":true}");
    return ESP_OK;
#endif
}

void bc250_ota_mark_running_valid(void)
{
#if CONFIG_BC250_OTA_ENABLED
    esp_ota_img_states_t state;
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (running && esp_ota_get_state_partition(running, &state) == ESP_OK &&
        state == ESP_OTA_IMG_PENDING_VERIFY) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_ota_mark_app_valid_cancel_rollback());
    }
#endif
}
