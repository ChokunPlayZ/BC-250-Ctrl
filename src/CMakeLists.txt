idf_component_register(
    SRCS
        "app_main.c"
        "app_events.c"
        "ble_presence.c"
        "button_service.c"
        "config_store.c"
        "gpio_service.c"
        "ota_service.c"
        "power_service.c"
        "status_led.c"
        "web_server.c"
        "wifi_service.c"
        "zigbee_service.c"
        "core/ble_match.c"
        "core/button_logic.c"
        "core/power_logic.c"
        "core/presence_logic.c"
    INCLUDE_DIRS
        "."
        "core"
    REQUIRES
        app_update
        bt
        esp_event
        esp_http_server
        esp_netif
        esp_timer
        esp_wifi
        espressif__cjson
        lwip
        mbedtls
        nvs_flash
        esp-zigbee-lib
)

target_compile_definitions(${COMPONENT_LIB} PRIVATE BC250_VERSION="${PROJECT_VER}")
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror=return-type)
