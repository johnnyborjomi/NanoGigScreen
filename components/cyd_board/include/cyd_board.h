/*
 * ESP32-2432S028 "Cheap Yellow Display" bring-up: 2.8" 320x240 SPI panel
 * (ILI9341 on the classic board, ST7789 on some 2-USB batches; Kconfig),
 * XPT2046 resistive touch on its own SPI bus, backlight, RGB LED.
 * Registers the display and touch with esp_lvgl_port.
 *
 * Pinout: github.com/witnessmenow/ESP32-Cheap-Yellow-Display (PINS.md).
 */
#ifndef CYD_BOARD_H
#define CYD_BOARD_H

#include <stdbool.h>
#include <stdint.h>

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CYD_H_RES 320
#define CYD_V_RES 240

/* Display SPI (HSPI) */
#define CYD_LCD_MOSI 13
#define CYD_LCD_MISO 12
#define CYD_LCD_SCLK 14
#define CYD_LCD_CS 15
#define CYD_LCD_DC 2
#define CYD_LCD_RST -1
#define CYD_LCD_BACKLIGHT 21

/* Touch SPI (separate bus, bit-banged by most Arduino sketches; a real SPI host here) */
#define CYD_TOUCH_MOSI 32
#define CYD_TOUCH_MISO 39
#define CYD_TOUCH_SCLK 25
#define CYD_TOUCH_CS 33
#define CYD_TOUCH_IRQ 36

/* RGB LED, active low */
#define CYD_LED_R 4
#define CYD_LED_G 16
#define CYD_LED_B 17

/* Initialise the panel, touch and LVGL port. Returns the LVGL display or NULL. */
lv_display_t *cyd_board_init(void);
void cyd_backlight(bool on);
/* Each channel on/off (the LED has no PWM here). */
void cyd_led(bool r, bool g, bool b);

#ifdef __cplusplus
}
#endif
#endif
