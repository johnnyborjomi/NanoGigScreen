idf_component_register(
  SRCS "main.c"
  INCLUDE_DIRS "."
  REQUIRES nano_protocol nano_ble nano_ota nano_ui cyd_board nvs_flash esp_timer)
