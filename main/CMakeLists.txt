idf_component_register(
  SRCS "main.c" "app.c" "link.c" "pedal_in.c" "settings.c" "preset_select.c" "block_edits.c" "tempo.c" "tuner.c"
       "expression.c" "rename.c" "remote_page.c" "ir_page.c" "update_mode.c"
  INCLUDE_DIRS "."
  REQUIRES nano_protocol nano_ble nano_ota nano_ui cyd_board esp_lvgl_port nvs_flash esp_timer)
