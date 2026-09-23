cmake_minimum_required(VERSION 3.16)

set(PROJECT_VER "0.1.0")
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/src")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(bc250_ctrl)
