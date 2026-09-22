#pragma once

#include "esp_err.h"
#include "esp_http_server.h"

esp_err_t bc250_ota_handle_http(httpd_req_t *request);
void bc250_ota_mark_running_valid(void);

