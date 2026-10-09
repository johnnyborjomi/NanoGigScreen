/*
 * Settings, three pages: presets per bank, label style, mute outputs 1/2, expression indicators; display rotation
 * and brightness; firmware version and the way into the update view. Built on open.
 */
#include <stdio.h>

#include "ui_internal.h"
#include "ui_widgets.h"

#define BRIGHTNESS_MIN 1
#define BRIGHTNESS_MAX 10

static struct {
    bool rotate_180;
    uint8_t brightness;
    int page;     /* reopens where it was left */
} s = { .brightness = BRIGHTNESS_MAX };

static struct {
    ui_pager_t pager;
    lv_obj_t *bank_value, *style_hint, *bright_value, *fw_value;
    ui_segments_t style, rotation;
    ui_switch_t mute, exp;
} w;

static void on_back(lv_event_t *e) { (void)e; ui_go(NANO_VIEW_MENU); }
static void on_close(lv_event_t *e) { (void)e; ui_go_base(); }
static void on_open_update(lv_event_t *e) { (void)e; ui_go(NANO_VIEW_UPDATE); }

static void on_bank_step(lv_event_t *e)
{
    int v = (int)g_ui.per_bank + (int)(intptr_t)lv_event_get_user_data(e);
    if (v < 2 || v > 8) return;
    g_ui.per_bank = (uint8_t)v;
    settings_refresh();
    if (g_ui.cb.on_bank_size) g_ui.cb.on_bank_size(g_ui.per_bank);
}

static void on_style_clicked(lv_event_t *e)
{
    uint8_t style = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
    if (style > NANO_LABEL_NUMERIC || style == g_ui.label_style) return;
    g_ui.label_style = (nano_label_style_t)style;
    settings_refresh();
    if (g_ui.cb.on_label_style) g_ui.cb.on_label_style(style);
}

static void on_mute_clicked(lv_event_t *e)
{
    (void)e;
    g_ui.outputs_muted = !g_ui.outputs_muted; /* optimistic; the pedal's settings reply confirms */
    main_refresh_mute();
    settings_refresh();
    if (g_ui.cb.on_outputs_mute) g_ui.cb.on_outputs_mute(g_ui.outputs_muted);
}

static void on_exp_clicked(lv_event_t *e)
{
    (void)e;
    g_ui.exp_show = !g_ui.exp_show;
    settings_refresh();
    main_refresh_expression();
    if (g_ui.cb.on_expression_show) g_ui.cb.on_expression_show(g_ui.exp_show);
}

static void on_rotation_clicked(lv_event_t *e)
{
    bool rotate = (uintptr_t)lv_event_get_user_data(e) != 0;
    if (rotate == s.rotate_180) return;
    s.rotate_180 = rotate;
    settings_refresh();
    if (g_ui.cb.on_rotation) g_ui.cb.on_rotation(rotate);
}

static void on_brightness_step(lv_event_t *e)
{
    int v = (int)s.brightness + (int)(intptr_t)lv_event_get_user_data(e);
    if (v < BRIGHTNESS_MIN || v > BRIGHTNESS_MAX) return;
    s.brightness = (uint8_t)v;
    settings_refresh();
    if (g_ui.cb.on_brightness) g_ui.cb.on_brightness(s.brightness);
}

static void build_page_presets(lv_obj_t *page)
{
    /* Presets per bank:  [-] 4 [+] */
    int32_t y = 4;
    ui_setting_caption(page, y, "Presets per bank");
    w.bank_value = ui_setting_stepper(page, y, on_bank_step);

    /* Preset label style: [1B] [A2] [1..64] with a one-line meaning under it. */
    y = 52;
    ui_setting_caption(page, y, "Preset label");
    static const char *const STYLES[3] = { "1B", "A2", "1..64" };
    const int32_t seg_w = 54, seg_gap = 4;
    w.style = ui_setting_segments(page, y, STYLES, 3, seg_w, on_style_clicked);
    w.style_hint = ui_label(page, &lv_font_montserrat_12, C_MUTED);
    lv_obj_set_pos(w.style_hint, SETTING_RIGHT - 3 * seg_w - 2 * seg_gap, y + SETTING_ROW_H + 3);
    lv_obj_set_width(w.style_hint, 3 * seg_w + 2 * seg_gap);
    lv_obj_set_style_text_align(w.style_hint, LV_TEXT_ALIGN_CENTER, 0);

    /* Mute outputs 1/2: a switch, red while muted (it silences the whole rig). */
    y = 118;
    ui_setting_caption(page, y, "Mute outputs 1/2");
    w.mute = ui_setting_switch(page, y, on_mute_clicked);

    /* Expression pedal indicators: the side bar and the tile tracks (~40 redraws/s while the pedal moves). */
    y = 158;
    ui_setting_caption(page, y, "Show expression pedal");
    w.exp = ui_setting_switch(page, y, on_exp_clicked);
}

static void build_page_display(lv_obj_t *page)
{
    /* Rotate display: [0°] [180°] (the USB lead leaves left or right, depending on the mount). */
    int32_t y = 4;
    ui_setting_caption(page, y, "Rotate display");
    static const char *const ROTATIONS[2] = { "0°", "180°" };
    w.rotation = ui_setting_segments(page, y, ROTATIONS, 2, 70, on_rotation_clicked);

    /* Brightness: [-] 10 [+], ten steps, applied as you tap. */
    y = 52;
    ui_setting_caption(page, y, "Brightness");
    w.bright_value = ui_setting_stepper(page, y, on_brightness_step);
}

static void build_page_firmware(lv_obj_t *page)
{
    int32_t y = 4;
    ui_setting_caption(page, y, "Firmware");
    w.fw_value = ui_label(page, &lv_font_montserrat_14, C_MUTED);
    lv_obj_set_width(w.fw_value, 150);
    lv_obj_set_style_text_align(w.fw_value, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_long_mode(w.fw_value, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(w.fw_value, SETTING_RIGHT - 150, y + (SETTING_ROW_H - lv_font_get_line_height(&lv_font_montserrat_14)) / 2);
    y = 52;
    ui_button(page, 0, y, SETTING_RIGHT, 40, LV_SYMBOL_DOWNLOAD "  Check for updates", &lv_font_montserrat_14, C_PANEL, C_ACCENT, on_open_update, NULL);
    lv_obj_t *hint = ui_label(page, &lv_font_montserrat_12, C_MUTED);
    lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(hint, SETTING_RIGHT);
    lv_obj_set_pos(hint, 0, y + 48);
    lv_label_set_text(hint, "Joins Wi-Fi and turns Bluetooth off while it runs. The screen restarts when you close the update page.");
}

static void remember_page(int idx) { s.page = idx; }

static lv_obj_t *build(lv_obj_t *scr)
{
    ui_overlay_t o = ui_overlay(scr, "Settings", on_back, on_close);
    /* Pager under the title bar. */
    const int32_t top = TOP_H + 6, page_h = SCREEN_H - top - 6;
    ui_pager_create(&w.pager, o.root, top, page_h, "Page");
    build_page_presets(ui_pager_add_page(&w.pager, o.root, SETTING_X, top, SCREEN_W - SETTING_X, page_h));
    build_page_display(ui_pager_add_page(&w.pager, o.root, SETTING_X, top, SCREEN_W - SETTING_X, page_h));
    build_page_firmware(ui_pager_add_page(&w.pager, o.root, SETTING_X, top, SCREEN_W - SETTING_X, page_h));
    w.pager.on_show = remember_page;
    ui_pager_show(&w.pager, s.page);
    settings_refresh();
    return o.root;
}

static void destroy(void) { w.bank_value = NULL; }

ui_page_t page_settings = { .build = build, .destroy = destroy };

void settings_refresh(void)
{
    if (!w.bank_value) return;
    static const char *const HINTS[3] = { "Mvave Chocolate", "Other MIDI controllers", "As on the Nano Cortex" };
    char t[4];
    snprintf(t, sizeof(t), "%u", (unsigned)g_ui.per_bank);
    lv_label_set_text(w.bank_value, t);
    ui_segments_select(&w.style, g_ui.label_style);
    lv_label_set_text(w.style_hint, HINTS[g_ui.label_style < 3 ? g_ui.label_style : 0]);
    ui_switch_set(&w.mute, g_ui.outputs_muted, C_ERROR);
    ui_switch_set(&w.exp, g_ui.exp_show, C_WARN);
    ui_segments_select(&w.rotation, s.rotate_180);
    snprintf(t, sizeof(t), "%u", (unsigned)s.brightness);
    lv_label_set_text(w.bright_value, t);
    lv_label_set_text(w.fw_value, g_ui.fw_version);
}

void nano_ui_settings_page(int index)
{
    if (w.bank_value) ui_pager_show(&w.pager, index);
    else if (index >= 0 && index < UI_PAGER_MAX_PAGES) s.page = index;
}

void nano_ui_set_rotation(bool rotate_180)
{
    s.rotate_180 = rotate_180;
    settings_refresh();
}

void nano_ui_set_brightness(uint8_t level)
{
    if (level < BRIGHTNESS_MIN || level > BRIGHTNESS_MAX) return;
    s.brightness = level;
    settings_refresh();
}
