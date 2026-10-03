cmake_minimum_required(VERSION 3.16)
# Firmware version (Settings page 3, and what the update check compares): release CI passes the tag
# in NANOGIG_VERSION; local builds fall back to ESP-IDF's `git describe`.
if(DEFINED ENV{NANOGIG_VERSION})
  set(PROJECT_VER $ENV{NANOGIG_VERSION})
endif()
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(nanogig_screen)
