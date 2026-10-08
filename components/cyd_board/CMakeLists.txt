idf_component_register(
  SRCS "cyd_board.c"
  INCLUDE_DIRS "include"
  REQUIRES esp_lcd driver esp_driver_ledc lvgl esp_lvgl_port
  PRIV_REQUIRES esp_lcd_touch_xpt2046 esp_lcd_ili9341 esp_timer)
