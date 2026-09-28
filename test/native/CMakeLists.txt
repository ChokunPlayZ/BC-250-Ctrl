cmake_minimum_required(VERSION 3.16)

project(bc250_core_tests LANGUAGES C)

enable_testing()

add_executable(
    bc250_core_tests
    test_core/test_main.c
    ../../src/core/ble_match.c
    ../../src/core/button_logic.c
    ../../src/core/power_logic.c
    ../../src/core/presence_logic.c
    ../../src/core/hp_commonslot_protocol.c
)

target_compile_features(bc250_core_tests PRIVATE c_std_11)
target_compile_options(bc250_core_tests PRIVATE -Wall -Wextra -Werror)
target_include_directories(bc250_core_tests PRIVATE ../../src/core)

add_test(NAME bc250_core_tests COMMAND bc250_core_tests)

add_executable(bc250_shell_tests
    test_shell/test_main.c
    ../../src/core/shell_args.c
    ../../src/serial_config.c
)
target_compile_features(bc250_shell_tests PRIVATE c_std_11)
target_compile_options(bc250_shell_tests PRIVATE -Wall -Wextra -Werror)
target_include_directories(bc250_shell_tests PRIVATE support ../../src ../../src/core)
add_test(NAME bc250_shell_tests COMMAND bc250_shell_tests)

foreach(target IN ITEMS ESP32C5 ESP32C6)
    add_executable(bc250_config_tests_${target}
        test_config/test_main.c
        ../../src/config_validation.c
    )
    target_compile_features(bc250_config_tests_${target} PRIVATE c_std_11)
    target_compile_options(bc250_config_tests_${target} PRIVATE -Wall -Wextra -Werror)
    target_compile_definitions(bc250_config_tests_${target} PRIVATE CONFIG_IDF_TARGET_${target}=1)
    target_include_directories(bc250_config_tests_${target} PRIVATE support ../../src)
    add_test(NAME bc250_config_tests_${target} COMMAND bc250_config_tests_${target})
endforeach()

add_executable(bc250_led_tests test_led/test_main.c ../../src/status_led.c)
target_compile_features(bc250_led_tests PRIVATE c_std_11)
target_compile_options(bc250_led_tests PRIVATE -Wall -Wextra -Werror)
target_include_directories(bc250_led_tests PRIVATE test_wifi/include support ../../src)
add_test(NAME bc250_led_tests COMMAND bc250_led_tests)

add_executable(bc250_wifi_tests test_wifi/test_main.c ../../src/wifi_service.c)
target_compile_features(bc250_wifi_tests PRIVATE c_std_11)
target_compile_options(bc250_wifi_tests PRIVATE -Wall -Wextra -Werror)
target_include_directories(bc250_wifi_tests PRIVATE test_wifi/include support ../../src)
foreach(scenario IN ITEMS wifi zigbee hybrid first_boot recovery_wifi recovery_zigbee expiry_wifi expiry_zigbee expiry_hybrid retry connected multiple_clients reconnect query_error repeat wrap stop_failure task_failure stale manual_close)
    add_test(NAME bc250_wifi_${scenario} COMMAND bc250_wifi_tests ${scenario})
endforeach()
