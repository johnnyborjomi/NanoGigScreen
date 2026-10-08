idf_component_register(
  SRCS "nano_ui.c" "ui_common.c" "ui_value_ctrl.c" "fonts/montserrat_medium_10.c" "fonts/montserrat_medium_12.c" "fonts/montserrat_bold_10.c"
  INCLUDE_DIRS "include"
  REQUIRES nano_protocol lvgl)
