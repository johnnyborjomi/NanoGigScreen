idf_component_register(
  SRCS "cyd_board.c"
  INCLUDE_DIRS "include"
  REQUIRES lvgl
  PRIV_REQUIRES esp_lcd esp_driver_gpio esp_driver_spi esp_driver_ledc esp_lvgl_port esp_lcd_touch_xpt2046
                esp_lcd_ili9341 esp_timer)
