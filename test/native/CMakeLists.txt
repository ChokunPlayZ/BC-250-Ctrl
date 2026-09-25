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
