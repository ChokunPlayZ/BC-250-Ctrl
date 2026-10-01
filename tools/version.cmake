# Keep the ESP-IDF application descriptor and BC250_VERSION in sync with Git.
# GitHub Releases are built from their tag, so an exact tag becomes the version.
find_package(Git QUIET)
set(PROJECT_VER "0.0.0+unknown")
if(DEFINED ENV{BC250_RELEASE_VERSION} AND NOT "$ENV{BC250_RELEASE_VERSION}" STREQUAL "")
    set(PROJECT_VER "$ENV{BC250_RELEASE_VERSION}")
elseif(GIT_FOUND)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" -C "${CMAKE_CURRENT_LIST_DIR}/.."
                describe --tags --always --dirty --abbrev=7 --match "v[0-9]*"
        OUTPUT_VARIABLE bc250_git_version
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
        RESULT_VARIABLE bc250_git_result
    )
    if(bc250_git_result EQUAL 0 AND NOT bc250_git_version STREQUAL "")
        set(PROJECT_VER "${bc250_git_version}")
    endif()
endif()

# esp_app_desc_t.version has 32 bytes, including the terminating NUL.
string(LENGTH "${PROJECT_VER}" bc250_version_length)
if(bc250_version_length GREATER 31)
    if(PROJECT_VER MATCHES "-[0-9]+-g[0-9a-f]+(-dirty)?$")
        # A long prerelease tag can fit exactly but exceed the field after a commit.
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" -C "${CMAKE_CURRENT_LIST_DIR}/.."
                    rev-parse --short=7 HEAD
            OUTPUT_VARIABLE bc250_short_sha
            OUTPUT_STRIP_TRAILING_WHITESPACE
            RESULT_VARIABLE bc250_short_sha_result
        )
        if(NOT bc250_short_sha_result EQUAL 0)
            message(FATAL_ERROR "Could not determine the commit hash for a short firmware version")
        endif()
        set(PROJECT_VER "${bc250_short_sha}")
        if(bc250_git_version MATCHES "-dirty$")
            string(APPEND PROJECT_VER "-dirty")
        endif()
    else()
        message(FATAL_ERROR "Firmware version '${PROJECT_VER}' exceeds ESP-IDF's 31-character limit")
    endif()
endif()
message(STATUS "BC-250 firmware version: ${PROJECT_VER}")
