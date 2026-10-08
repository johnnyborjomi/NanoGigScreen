/*
 * Shared look and building blocks for the nano_ui pages and components (private to nano_ui):
 * the NanoGig palette, the screen size, the custom fonts and the small object constructors every
 * page uses. New pages and components include this instead of growing nano_ui.c.
 */
#ifndef UI_COMMON_H
#define UI_COMMON_H

#include <stdint.h>

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* NanoGig palette (src/ui/styles.css). */
#define C_BG 0x07090C
#define C_PANEL 0x11151B
#define C_PANEL_2 0x181E26
#define C_TEXT 0xF4F6F8
#define C_MUTED 0x8D97A5
#define C_DIM 0x4D5661
#define C_ON 0x2DD4A0
#define C_OFF 0x2A313B
#define C_OFF_TEXT 0x6B7583
#define C_WARN 0xFFB454
#define C_ERROR 0xFF5C6C
#define C_ACCENT 0x5AA9FF
#define C_FX_TEXT 0x0B0D10

#define SCREEN_W 320
#define SCREEN_H 240

/* Montserrat Medium (weight 500) for the tiles and the gate button, generated with lv_font_conv
 * from JulietaUla/Montserrat (OFL): tools/fonts.md has the command. */
LV_FONT_DECLARE(montserrat_medium_10)
LV_FONT_DECLARE(montserrat_medium_12)
/* Montserrat Bold 10 for the category tag at the top of each FX tile. */
LV_FONT_DECLARE(montserrat_bold_10)

/* An empty label in `font` / `color`. */
lv_obj_t *ui_label(lv_obj_t *parent, const lv_font_t *font, uint32_t color);
/* A plain box: no theme styles, opaque `bg`, not scrollable. */
lv_obj_t *ui_box(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t bg);
/* A round status dot, C_DIM until coloured. */
lv_obj_t *ui_dot(lv_obj_t *parent, int32_t x, int32_t y, int32_t d);
/* A flat button with a centred label (lv_obj_get_child(b, 0)); `cb` runs on LV_EVENT_CLICKED. */
lv_obj_t *ui_button(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h, const char *text, const lv_font_t *font, uint32_t bg, uint32_t fg, lv_event_cb_t cb, void *user);
/* A one-line text field in the NanoGig look, with a blinking accent cursor. */
lv_obj_t *ui_text_field(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h, const lv_font_t *font);
/* A keyboard across the bottom of `parent`, `h` tall, typing into `ta`: four rows with bigger keys
 * than LVGL's default (no Enter, no hide key: OK sends LV_EVENT_READY to `ta`), letters, digits and
 * every printable ASCII symbol over two symbol pages ("1#", then "#+="). */
lv_obj_t *ui_keyboard(lv_obj_t *parent, int32_t h, lv_obj_t *ta);
/* LV_EVENT_PRESSED handler that logs where a press started (touch debugging on the serial log). */
void ui_on_pressed(lv_event_t *e);

#ifdef __cplusplus
}
#endif
#endif
