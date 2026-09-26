# Host preview: renders the gig screen with real LVGL into PNG files, no hardware needed.
#   cmake -B build && cmake --build build -j8 && ./build/preview out/
cmake_minimum_required(VERSION 3.16)
project(nanogig_preview C)
set(CMAKE_C_STANDARD 11)
set(ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)
set(LVGL ${ROOT}/managed_components/lvgl__lvgl)

file(GLOB_RECURSE LVGL_SRC ${LVGL}/src/*.c)
list(FILTER LVGL_SRC EXCLUDE REGEX "/src/drivers/")
add_library(lvgl STATIC ${LVGL_SRC})
target_include_directories(lvgl PUBLIC ${LVGL} ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(lvgl PUBLIC LV_CONF_INCLUDE_SIMPLE)
target_compile_options(lvgl PRIVATE -w -O1)

add_subdirectory(${ROOT}/components/nano_protocol nano_protocol)

add_library(nano_ui STATIC ${ROOT}/components/nano_ui/nano_ui.c ${ROOT}/components/nano_ui/fonts/montserrat_medium_10.c ${ROOT}/components/nano_ui/fonts/montserrat_medium_12.c)
target_include_directories(nano_ui PUBLIC ${ROOT}/components/nano_ui/include)
target_link_libraries(nano_ui PUBLIC lvgl nano_protocol)

add_executable(preview preview.c)
target_include_directories(preview PRIVATE ${ROOT}/components/nano_protocol/test)
target_link_libraries(preview nano_ui lvgl nano_protocol m)
