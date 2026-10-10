idf_component_register(
  SRCS "nano_ui.c" "page_main.c" "page_connect.c" "page_menu.c" "page_settings.c" "page_presets.c" "page_source.c" "ir_tab.c"
       "page_rename.c" "page_ir_list.c" "page_fx.c" "page_gate.c" "ir_folders.c" "page_tuner.c" "page_tempo.c" "page_update.c" "ui_widgets.c" "ui_common.c" "ui_value_ctrl.c" "ui_text_edit.c" "fonts/montserrat_medium_10.c" "fonts/montserrat_medium_12.c" "fonts/montserrat_bold_10.c" "fonts/montserrat_medium_20_phase.c"
  INCLUDE_DIRS "include"
  REQUIRES nano_protocol lvgl)
