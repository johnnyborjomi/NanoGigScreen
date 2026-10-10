# Plain C, no ESP-IDF dependencies: the same sources build on the host (see test/).
if(COMMAND idf_component_register)
  idf_component_register(
    SRCS "nano_proto.c" "nano_frame.c" "nano_build.c" "nano_scales.c" "nano_assembler.c" "nano_models.c" "nano_fx_params.c" "nano_decode.c"
    INCLUDE_DIRS "include")
else()
  add_library(nano_protocol STATIC nano_proto.c nano_frame.c nano_build.c nano_scales.c nano_assembler.c nano_models.c nano_fx_params.c nano_decode.c)
  target_include_directories(nano_protocol PUBLIC include)
  target_compile_options(nano_protocol PRIVATE -Wall -Wextra -Werror)
  target_link_libraries(nano_protocol PUBLIC m)
endif()
