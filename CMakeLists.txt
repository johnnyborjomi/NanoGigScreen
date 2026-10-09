cmake_minimum_required(VERSION 3.16)
# Firmware version (Settings page 3, and what the update check compares): release CI passes the tag
# in NANOGIG_VERSION; local builds fall back to ESP-IDF's `git describe`.
if(DEFINED ENV{NANOGIG_VERSION})
  set(PROJECT_VER $ENV{NANOGIG_VERSION})
endif()
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(nanogig_screen)
# The image is built for size (sdkconfig.defaults). LVGL keeps -O2 so drawing stays as fast as before, and
# mbedTLS so the TLS handshake (software ECC) does not hold CPU 0 long enough to trip the idle watchdog.
idf_component_get_property(lvgl_lib lvgl__lvgl COMPONENT_LIB)
target_compile_options(${lvgl_lib} PRIVATE -O2)
foreach(lib mbedtls mbedx509 mbedcrypto)
  target_compile_options(${lib} PRIVATE -O2)
endforeach()
