set(bc250_supported_targets esp32 esp32s3 esp32c3 esp32c5 esp32c6 esp32c61 esp32h2 esp32h21 esp32h4)
if(NOT IDF_TARGET IN_LIST bc250_supported_targets)
    message(FATAL_ERROR "BC-250 requires a supported BLE-capable ESP32 target, got ${IDF_TARGET}")
endif()

if(IDF_TARGET MATCHES "^(esp32c5|esp32c6|esp32h2|esp32h21|esp32h4)$")
    set(radio_sources "zigbee_runtime.c" "zigbee_service.c")
    set(radio_requires esp-zigbee-lib)
else()
    set(radio_sources "zigbee_service_stub.c")
endif()

if(IDF_TARGET MATCHES "^(esp32h2|esp32h21|esp32h4)$")
    set(network_sources "wifi_service_stub.c")
    set(network_requires)
else()
    set(network_sources "wifi_service.c" "web_server.c")
    set(network_requires esp_http_server esp_netif esp_wifi lwip)
endif()

idf_component_register(
    SRCS
        "app_main.c"
        "app_events.c"
        "ble_presence.c"
        "button_service.c"
        "config_store.c"
        "config_validation.c"
        "gpio_service.c"
        "i2c_service.c"
        "ota_service.c"
        "power_service.c"
        "psu_i2c_service.c"
        "serial_config.c"
        "serial_service.c"
        "status_service.c"
        "status_led.c"
        ${network_sources}
        ${radio_sources}
        "core/ble_match.c"
        "core/button_logic.c"
        "core/power_logic.c"
        "core/presence_logic.c"
        "core/hp_commonslot_protocol.c"
        "core/hp_commonslot_identity.c"
        "core/shell_args.c"
    INCLUDE_DIRS
        "."
        "core"
    EMBED_TXTFILES "web_ui.html"
    REQUIRES
        app_update
        bt
        console
        esp_event
        esp_driver_gpio
        esp_driver_i2c
        esp_driver_uart
        esp_driver_usb_serial_jtag
        esp_timer
        espressif__cjson
        mbedtls
        nvs_flash
        ${network_requires}
        ${radio_requires}
)

target_compile_definitions(${COMPONENT_LIB} PRIVATE BC250_VERSION="${PROJECT_VER}")
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror=return-type)
