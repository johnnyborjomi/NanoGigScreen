/* The screen's own settings and the pedal's name cache, kept in flash (NVS namespace "nanogig"). */
#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Load everything; the name cache goes to g_app.meta. Call once, after nvs_flash_init. */
void settings_load(void);

uint8_t settings_bank_size(void);   /* presets per bank, 2..8 */
uint8_t settings_label_style(void); /* nano_label_style_t */
bool settings_exp_show(void);       /* draw the expression indicators */
bool settings_rotate_180(void);
uint8_t settings_brightness(void);  /* CYD_BRIGHTNESS_MIN..MAX */

void settings_set_bank_size(uint8_t v);
void settings_set_label_style(uint8_t v);
void settings_set_exp_show(bool show);
void settings_set_rotation(bool rotate_180); /* marks UI_ROTATION */
void settings_set_brightness(uint8_t v);     /* applied now, saved once the slider rests */
void settings_flush(void);                   /* write what is still pending (before a restart) */
void settings_tick(int64_t now);
void settings_ui_push(uint32_t parts);

/* Write g_app.meta to flash (after a metadata dump that changed it, or a rename). */
void settings_save_meta(void);
