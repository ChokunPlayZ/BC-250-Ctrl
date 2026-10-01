#pragma once

#include "esp_err.h"
#include "target_caps.h"
#if BC250_HAS_WIFI
#include "esp_http_server.h"
esp_err_t bc250_ota_handle_http(httpd_req_t *request);
#endif
void bc250_ota_mark_running_valid(void);
