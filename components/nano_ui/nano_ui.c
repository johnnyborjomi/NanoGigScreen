#include "nano_ui.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "nano_models.h"

/* Montserrat Medium (weight 500) for the tiles and the gate button, generated with lv_font_conv
 * from JulietaUla/Montserrat (OFL): tools/fonts.md has the command. */
LV_FONT_DECLARE(montserrat_medium_10)
LV_FONT_DECLARE(montserrat_medium_12)

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

/* Slot colours for the preset label (1A red, 1B orange, 1C green, 1D cyan ...). */
static const uint32_t SLOT_COLORS[8] = { 0xFF5C5C, 0xFFB454, 0x4CF06A, 0x00F0D8, 0x3D9BFF, 0xA78BFA, 0xF050C8, 0xF4F6F8 };

#define SCREEN_W 320
#define SCREEN_H 240
#define TOP_H 22
#define ROW_Y 24
#define ROW_H 94
#define NAV_W 36          /* prev / next buttons: narrower and shorter than the row, centred on it */
#define NAV_H 66
#define LINES_Y 126
#define LINES_H 40
#define EDGE_X 2          /* left edge shared by the prev button, the gate button and the first tile */
#define GATE_W 40
#define GATE_H 26
#define TILE_Y 176
#define TILE_W 60
#define TILE_H 60

static nano_ui_callbacks_t s_cb;
static lv_obj_t *s_main, *s_menu, *s_settings, *s_tuner;
static nano_view_t s_view = NANO_VIEW_MAIN;

/* main view */
static lv_obj_t *s_status_dot, *s_status, *s_tempo, *s_gate;
static bool s_gate_on;
static lv_obj_t *s_preset_label, *s_preset_name, *s_prev, *s_next;
static lv_obj_t *s_capture_dot, *s_capture, *s_ir_dot, *s_ir;
static lv_obj_t *s_tiles[NANO_FX_SLOT_COUNT], *s_tile_names[NANO_FX_SLOT_COUNT];
static bool s_tile_present[NANO_FX_SLOT_COUNT];
static bool s_tile_on[NANO_FX_SLOT_COUNT];
static uint8_t s_preset;
static uint8_t s_per_bank = 4;
/* Footswitch badges (web app: IA / IIA yellow, IB / IIB indigo), shown under the bank label. */
static lv_obj_t *s_fs_badges[4];
static uint8_t s_footswitch[4];
static bool s_footswitch_known;
static const char *const FS_NAMES[4] = { "IA", "IB", "IIA", "IIB" };
static const uint32_t FS_BG[4] = { 0xF5C542, 0x6A5CFF, 0xF5C542, 0x6A5CFF };
static const uint32_t FS_FG[4] = { 0x111111, 0xFFFFFF, 0x111111, 0xFFFFFF };
#define BADGE_W 26
#define BADGE_H 14
#define BADGE_GAP 3
static bool s_link_enabled = true;
static lv_point_t s_press_point;

/* menu / settings / tuner */
static lv_obj_t *s_link_btn_label, *s_bank_value;
static lv_obj_t *s_tuner_note, *s_tuner_cents, *s_tuner_bar, *s_tuner_verdict, *s_tuner_mute;
static bool s_tuner_muted;

/* ---- helpers -------------------------------------------------------------- */

static lv_obj_t *make_label(lv_obj_t *parent, const lv_font_t *font, uint32_t color)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_label_set_text(l, "");
    return l;
}

static lv_obj_t *make_box(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t bg)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(o, lv_color_hex(bg), 0);
    lv_obj_set_scrollable(o, false);
    return o;
}

static lv_obj_t *make_dot(lv_obj_t *parent, int32_t x, int32_t y, int32_t d)
{
    lv_obj_t *o = make_box(parent, x, y, d, d, C_DIM);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
    return o;
}

/* A flat button with a centred label; returns the button, *label_out the label. */
static lv_obj_t *make_button(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h, const char *text, const lv_font_t *font, uint32_t bg, uint32_t fg, lv_event_cb_t cb, void *user)
{
    lv_obj_t *b = make_box(parent, x, y, w, h, bg);
    lv_obj_set_style_radius(b, 10, 0);
    lv_obj_set_clickable(b, true);
    lv_obj_set_style_bg_color(b, lv_color_hex(fg), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(b, LV_OPA_30, LV_STATE_PRESSED);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, user);
    lv_obj_t *l = make_label(b, font, fg);
    lv_label_set_text(l, text);
    lv_obj_center(l);
    return b;
}

/* Any press: remember where it started (the release sample of a resistive panel drifts). */
static void on_pressed(lv_event_t *e)
{
    lv_indev_t *indev = lv_event_get_indev(e);
    if (!indev) return;
    lv_indev_get_point(indev, &s_press_point);
    printf("touch press x=%d y=%d\n", (int)s_press_point.x, (int)s_press_point.y);
}

/* Largest font whose wrapped text fits the box in at most two lines. */
static const lv_font_t *fit_font(const char *text, int32_t max_w, int32_t max_h)
{
    static const lv_font_t *const fonts[] = { &lv_font_montserrat_40, &lv_font_montserrat_32, &lv_font_montserrat_28, &lv_font_montserrat_24, &lv_font_montserrat_20 };
    for (size_t i = 0; i < sizeof(fonts) / sizeof(fonts[0]); i++) {
        lv_point_t size;
        lv_text_get_size(&size, text, fonts[i], 0, 0, max_w, LV_TEXT_FLAG_NONE);
        int32_t line_h = lv_font_get_line_height(fonts[i]);
        if (size.y <= max_h && size.y <= 2 * line_h + 1) return fonts[i];
    }
    return &lv_font_montserrat_20;
}

/* Tile names wrap on spaces; a single word wider than the tile drops to the 10 px font. */
static const lv_font_t *tile_font(const char *text, int32_t max_w)
{
    const char *p = text;
    while (*p) {
        const char *end = p;
        while (*end && *end != ' ') end++;
        char word[32];
        size_t n = (size_t)(end - p) < sizeof(word) - 1 ? (size_t)(end - p) : sizeof(word) - 1;
        memcpy(word, p, n);
        word[n] = '\0';
        lv_point_t size;
        lv_text_get_size(&size, word, &montserrat_medium_12, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        if (size.x > max_w) return &montserrat_medium_10;
        p = *end ? end + 1 : end;
    }
    return &montserrat_medium_12;
}

/* ---- callbacks ------------------------------------------------------------ */

static void on_prev(lv_event_t *e) { (void)e; if (s_cb.on_prev_preset) s_cb.on_prev_preset(); }
static void on_next(lv_event_t *e) { (void)e; if (s_cb.on_next_preset) s_cb.on_next_preset(); }
static void on_menu(lv_event_t *e) { (void)e; nano_ui_show(NANO_VIEW_MENU); }
static void on_close(lv_event_t *e) { (void)e; nano_ui_show(NANO_VIEW_MAIN); }
static void on_open_settings(lv_event_t *e) { (void)e; nano_ui_show(NANO_VIEW_SETTINGS); }
static void on_open_tuner(lv_event_t *e) { (void)e; nano_ui_show(NANO_VIEW_TUNER); }
static void on_back_to_menu(lv_event_t *e) { (void)e; nano_ui_show(NANO_VIEW_MENU); }

static void on_link_toggle(lv_event_t *e)
{
    (void)e;
    s_link_enabled = !s_link_enabled;
    nano_ui_set_link_enabled(s_link_enabled);
    if (s_cb.on_link) s_cb.on_link(s_link_enabled);
    nano_ui_show(NANO_VIEW_MAIN);
}

static void on_gate_clicked(lv_event_t *e)
{
    (void)e;
    if (s_cb.on_toggle_gate) s_cb.on_toggle_gate(s_gate_on);
}

static void on_mute_clicked(lv_event_t *e)
{
    (void)e;
    if (s_cb.on_tuner_mute) s_cb.on_tuner_mute(!s_tuner_muted);
}

static void on_tile_clicked(lv_event_t *e)
{
    uint8_t slot = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
    if (slot >= NANO_FX_SLOT_COUNT || !s_tile_present[slot]) return;
    if (s_cb.on_toggle_fx) s_cb.on_toggle_fx(slot, s_tile_on[slot]);
}

static void set_bank_value_text(void)
{
    char t[4];
    snprintf(t, sizeof(t), "%u", (unsigned)s_per_bank);
    lv_label_set_text(s_bank_value, t);
}

static void on_bank_step(lv_event_t *e)
{
    int delta = (int)(intptr_t)lv_event_get_user_data(e);
    int v = (int)s_per_bank + delta;
    if (v < 2 || v > 8) return;
    s_per_bank = (uint8_t)v;
    set_bank_value_text();
    if (s_cb.on_bank_size) s_cb.on_bank_size(s_per_bank);
}

/* ---- main view ------------------------------------------------------------ */

static void build_main(lv_obj_t *scr)
{
    s_main = make_box(scr, 0, 0, SCREEN_W, SCREEN_H, C_BG);
    lv_obj_add_event_cb(s_main, on_pressed, LV_EVENT_PRESSED, NULL);

    /* Top bar: status dot + text, tempo, gate, menu button. */
    s_status_dot = make_dot(s_main, 8, 8, 8);
    s_status = make_label(s_main, &lv_font_montserrat_12, C_MUTED);
    lv_obj_set_pos(s_status, 22, 5);
    lv_obj_set_width(s_status, 140);
    lv_label_set_long_mode(s_status, LV_LABEL_LONG_DOT);
    s_tempo = make_label(s_main, &lv_font_montserrat_12, C_ON);
    lv_obj_set_pos(s_tempo, 190, 5);
    lv_obj_set_width(s_tempo, 80);
    lv_obj_set_style_text_align(s_tempo, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_t *menu = make_button(s_main, SCREEN_W - 44, 0, 44, TOP_H + 4, LV_SYMBOL_LIST, &lv_font_montserrat_14, C_BG, C_MUTED, on_menu, NULL);
    lv_obj_add_event_cb(menu, on_pressed, LV_EVENT_PRESSED, NULL);
    lv_obj_set_ext_click_area(menu, 6);

    /* Preset row: prev | label + name | next. */
    const int32_t nav_y = ROW_Y + (ROW_H - NAV_H) / 2;
    s_prev = make_button(s_main, EDGE_X, nav_y, NAV_W, NAV_H, LV_SYMBOL_LEFT, &lv_font_montserrat_20, C_PANEL, C_MUTED, on_prev, NULL);
    s_next = make_button(s_main, SCREEN_W - EDGE_X - NAV_W, nav_y, NAV_W, NAV_H, LV_SYMBOL_RIGHT, &lv_font_montserrat_20, C_PANEL, C_MUTED, on_next, NULL);
    lv_obj_add_event_cb(s_prev, on_pressed, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(s_next, on_pressed, LV_EVENT_PRESSED, NULL);
    s_preset_label = make_label(s_main, &lv_font_montserrat_24, C_TEXT);
    lv_obj_set_pos(s_preset_label, NAV_W + 6, ROW_Y + 4);
    s_preset_name = make_label(s_main, &lv_font_montserrat_32, C_TEXT);
    lv_label_set_long_mode(s_preset_name, LV_LABEL_LONG_WRAP);
    lv_label_set_text(s_preset_name, "NanoGig");
    for (int i = 0; i < 4; i++) {
        lv_obj_t *b = make_box(s_main, 0, 0, BADGE_W, BADGE_H, FS_BG[i]);
        lv_obj_set_style_radius(b, 4, 0);
        lv_obj_t *l = make_label(b, &montserrat_medium_10, FS_FG[i]);
        lv_label_set_text(l, FS_NAMES[i]);
        lv_obj_center(l);
        lv_obj_set_hidden(b, true);
        s_fs_badges[i] = b;
    }

    /* Gate button, then the capture / IR lines. */
    s_gate = make_button(s_main, EDGE_X, LINES_Y + (LINES_H - GATE_H) / 2, GATE_W, GATE_H, "GATE", &montserrat_medium_10, C_OFF, C_TEXT, on_gate_clicked, NULL);
    lv_obj_set_style_radius(s_gate, 7, 0);
    lv_obj_add_event_cb(s_gate, on_pressed, LV_EVENT_PRESSED, NULL);
    lv_obj_set_ext_click_area(s_gate, 8);
    const int32_t lx = EDGE_X + GATE_W + 10;
    s_capture_dot = make_dot(s_main, lx, LINES_Y + 6, 9);
    s_capture = make_label(s_main, &lv_font_montserrat_12, C_TEXT);
    lv_obj_set_pos(s_capture, lx + 15, LINES_Y + 2);
    lv_obj_set_width(s_capture, SCREEN_W - lx - 15 - 8);
    lv_label_set_long_mode(s_capture, LV_LABEL_LONG_DOT);
    s_ir_dot = make_dot(s_main, lx, LINES_Y + 26, 9);
    s_ir = make_label(s_main, &lv_font_montserrat_12, C_MUTED);
    lv_obj_set_pos(s_ir, lx + 15, LINES_Y + 22);
    lv_obj_set_width(s_ir, SCREEN_W - lx - 15 - 8);
    lv_label_set_long_mode(s_ir, LV_LABEL_LONG_DOT);

    /* Five FX tiles: pre1 pre2 | post1 post2 post3. */
    const int32_t gap = 3, group_gap = 4; /* 5 x 60 + 4 x 3 + 4 = 316: from EDGE_X to 318 */
    for (int i = 0; i < NANO_FX_SLOT_COUNT; i++) {
        lv_obj_t *t = make_box(s_main, EDGE_X + i * (TILE_W + gap) + (i >= 2 ? group_gap : 0), TILE_Y, TILE_W, TILE_H, C_PANEL);
        lv_obj_set_style_radius(t, 10, 0);
        lv_obj_set_style_border_width(t, 3, 0);
        lv_obj_set_style_border_side(t, LV_BORDER_SIDE_TOP, 0);
        lv_obj_set_style_border_color(t, lv_color_hex(C_DIM), 0);
        lv_obj_set_style_pad_all(t, 2, 0);
        lv_obj_set_clickable(t, true);
        lv_obj_set_ext_click_area(t, 6);
        lv_obj_add_event_cb(t, on_pressed, LV_EVENT_PRESSED, NULL);
        lv_obj_add_event_cb(t, on_tile_clicked, LV_EVENT_CLICKED, (void *)(uintptr_t)i);
        lv_obj_t *name = make_label(t, &montserrat_medium_12, C_DIM);
        lv_obj_set_width(name, TILE_W - 4);
        lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_long_mode(name, LV_LABEL_LONG_WRAP);
        lv_obj_center(name);
        lv_label_set_text(name, i < 2 ? "PRE" : "POST");
        s_tiles[i] = t;
        s_tile_names[i] = name;
    }
}

/* ---- menu / settings / tuner views --------------------------------------- */

static lv_obj_t *make_overlay(lv_obj_t *scr, const char *title)
{
    lv_obj_t *o = make_box(scr, 0, 0, SCREEN_W, SCREEN_H, C_BG);
    lv_obj_add_event_cb(o, on_pressed, LV_EVENT_PRESSED, NULL);
    lv_obj_t *t = make_label(o, &lv_font_montserrat_14, C_MUTED);
    lv_label_set_text(t, title);
    lv_obj_set_pos(t, 12, 8);
    lv_obj_t *close = make_button(o, SCREEN_W - 44, 0, 44, TOP_H + 4, LV_SYMBOL_CLOSE, &lv_font_montserrat_14, C_BG, C_MUTED, on_close, NULL);
    lv_obj_set_ext_click_area(close, 6);
    lv_obj_set_hidden(o, true);
    return o;
}

static lv_obj_t *menu_item(lv_obj_t *parent, int row, const char *text, lv_event_cb_t cb, lv_obj_t **label_out)
{
    const int32_t h = 44, gap = 6, y0 = 30;
    lv_obj_t *b = make_button(parent, 12, y0 + row * (h + gap), SCREEN_W - 24, h, text, &lv_font_montserrat_20, C_PANEL, C_TEXT, cb, NULL);
    lv_obj_add_event_cb(b, on_pressed, LV_EVENT_PRESSED, NULL);
    if (label_out) *label_out = lv_obj_get_child(b, 0);
    return b;
}

static void build_menu(lv_obj_t *scr)
{
    s_menu = make_overlay(scr, "NanoGig Screen");
    menu_item(s_menu, 0, LV_SYMBOL_AUDIO "  Tuner", on_open_tuner, NULL);
    menu_item(s_menu, 1, LV_SYMBOL_SETTINGS "  Settings", on_open_settings, NULL);
    menu_item(s_menu, 2, LV_SYMBOL_BLUETOOTH "  Disconnect", on_link_toggle, &s_link_btn_label);
    menu_item(s_menu, 3, "Close", on_close, NULL);
}

static void build_settings(lv_obj_t *scr)
{
    s_settings = make_overlay(scr, "Settings");
    lv_obj_t *l = make_label(s_settings, &lv_font_montserrat_14, C_TEXT);
    lv_label_set_text(l, "Presets per bank");
    lv_obj_set_pos(l, 12, 44);
    lv_obj_t *hint = make_label(s_settings, &lv_font_montserrat_12, C_MUTED);
    lv_label_set_text(hint, "4 = Mvave Chocolate (1A..1D), 8 = A..H");
    lv_obj_set_pos(hint, 12, 64);
    lv_obj_t *minus = make_button(s_settings, 12, 92, 56, 48, LV_SYMBOL_MINUS, &lv_font_montserrat_20, C_PANEL, C_TEXT, on_bank_step, (void *)(intptr_t)-1);
    lv_obj_add_event_cb(minus, on_pressed, LV_EVENT_PRESSED, NULL);
    s_bank_value = make_label(s_settings, &lv_font_montserrat_32, C_TEXT);
    lv_obj_set_pos(s_bank_value, 76, 98);
    lv_obj_set_width(s_bank_value, 56);
    lv_obj_set_style_text_align(s_bank_value, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_t *plus = make_button(s_settings, 140, 92, 56, 48, LV_SYMBOL_PLUS, &lv_font_montserrat_20, C_PANEL, C_TEXT, on_bank_step, (void *)(intptr_t)1);
    lv_obj_add_event_cb(plus, on_pressed, LV_EVENT_PRESSED, NULL);
    set_bank_value_text();
    lv_obj_t *back = make_button(s_settings, 12, SCREEN_H - 56, SCREEN_W - 24, 44, "Back", &lv_font_montserrat_20, C_PANEL, C_TEXT, on_back_to_menu, NULL);
    lv_obj_add_event_cb(back, on_pressed, LV_EVENT_PRESSED, NULL);
}

static void build_tuner(lv_obj_t *scr)
{
    s_tuner = make_overlay(scr, "Tuner");
    s_tuner_note = make_label(s_tuner, &lv_font_montserrat_40, C_TEXT);
    lv_obj_set_width(s_tuner_note, SCREEN_W);
    lv_obj_set_style_text_align(s_tuner_note, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(s_tuner_note, 0, 40);
    lv_label_set_text(s_tuner_note, "-");
    /* Cents bar: -50..+50 with a centre mark. */
    lv_obj_t *track = make_box(s_tuner, 20, 110, SCREEN_W - 40, 14, C_PANEL_2);
    lv_obj_set_style_radius(track, 7, 0);
    lv_obj_t *centre = make_box(s_tuner, SCREEN_W / 2 - 1, 100, 2, 34, C_MUTED);
    (void)centre;
    s_tuner_bar = make_box(s_tuner, SCREEN_W / 2 - 6, 108, 12, 18, C_WARN);
    lv_obj_set_style_radius(s_tuner_bar, 6, 0);
    s_tuner_cents = make_label(s_tuner, &lv_font_montserrat_20, C_MUTED);
    lv_obj_set_width(s_tuner_cents, SCREEN_W);
    lv_obj_set_style_text_align(s_tuner_cents, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(s_tuner_cents, 0, 140);
    lv_label_set_text(s_tuner_cents, "");
    s_tuner_verdict = make_label(s_tuner, &lv_font_montserrat_14, C_MUTED);
    lv_obj_set_width(s_tuner_verdict, SCREEN_W);
    lv_obj_set_style_text_align(s_tuner_verdict, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(s_tuner_verdict, 0, 166);
    lv_label_set_text(s_tuner_verdict, "Play a note");
    /* Mute state, tappable: re-sends tuner-on with the other flag (Cortex Cloud does the same). */
    s_tuner_mute = make_button(s_tuner, SCREEN_W - 44 - 96, 4, 92, 22, "SOUND ON", &montserrat_medium_10, C_PANEL, C_MUTED, on_mute_clicked, NULL);
    lv_obj_set_style_radius(s_tuner_mute, 7, 0);
    lv_obj_add_event_cb(s_tuner_mute, on_pressed, LV_EVENT_PRESSED, NULL);
    lv_obj_set_ext_click_area(s_tuner_mute, 8);
    lv_obj_t *done = make_button(s_tuner, 12, SCREEN_H - 50, SCREEN_W - 24, 40, "Done", &lv_font_montserrat_20, C_PANEL, C_TEXT, on_close, NULL);
    lv_obj_add_event_cb(done, on_pressed, LV_EVENT_PRESSED, NULL);
}

/* ---- public --------------------------------------------------------------- */

void nano_ui_create(lv_display_t *disp, const nano_ui_callbacks_t *cb)
{
    s_cb = *cb;
    lv_obj_t *scr = lv_display_get_screen_active(disp);
    lv_obj_set_style_bg_color(scr, lv_color_hex(C_BG), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_scrollable(scr, false);
    build_main(scr);
    build_menu(scr);
    build_settings(scr);
    build_tuner(scr);
    nano_ui_set_link_enabled(true);
}

static void show_view(nano_view_t view, bool notify)
{
    if (view == s_view) return;
    if (s_view == NANO_VIEW_TUNER && notify && s_cb.on_tuner) s_cb.on_tuner(false);
    s_view = view;
    lv_obj_set_hidden(s_menu, view != NANO_VIEW_MENU);
    lv_obj_set_hidden(s_settings, view != NANO_VIEW_SETTINGS);
    lv_obj_set_hidden(s_tuner, view != NANO_VIEW_TUNER);
    if (view == NANO_VIEW_TUNER) {
        nano_ui_set_tuner(NULL, 0, false);
        if (notify && s_cb.on_tuner) s_cb.on_tuner(true);
    }
}

void nano_ui_show(nano_view_t view)
{
    show_view(view, true);
}

void nano_ui_open_tuner_from_pedal(void)
{
    show_view(NANO_VIEW_TUNER, false);
}

void nano_ui_set_tuner_mute(bool muted)
{
    s_tuner_muted = muted;
    lv_obj_t *l = lv_obj_get_child(s_tuner_mute, 0);
    lv_label_set_text(l, muted ? "MUTED" : "SOUND ON");
    lv_obj_set_style_text_color(l, lv_color_hex(muted ? C_WARN : C_MUTED), 0);
    lv_obj_set_style_bg_color(s_tuner_mute, lv_color_hex(muted ? 0x3A2A10 : C_PANEL), 0);
    lv_obj_center(l);
}

nano_view_t nano_ui_view(void)
{
    return s_view;
}

void nano_ui_set_status(const char *text, bool connected)
{
    lv_label_set_text(s_status, text);
    lv_obj_set_style_bg_color(s_status_dot, lv_color_hex(connected ? C_ON : C_WARN), 0);
}

void nano_ui_set_link_enabled(bool enabled)
{
    s_link_enabled = enabled;
    lv_label_set_text(s_link_btn_label, enabled ? LV_SYMBOL_BLUETOOTH "  Disconnect" : LV_SYMBOL_BLUETOOTH "  Connect");
    lv_obj_center(s_link_btn_label);
}

void nano_ui_set_bank_size(uint8_t per_bank)
{
    if (per_bank < 2 || per_bank > 8) return;
    s_per_bank = per_bank;
    set_bank_value_text();
}

static void layout_preset_row(void)
{
    /* Which footswitch badges apply to the shown preset. */
    int shown[4], n = 0;
    if (s_footswitch_known && lv_label_get_text(s_preset_label)[0]) {
        for (int i = 0; i < 4; i++) if (s_footswitch[i] == s_preset) shown[n++] = i;
    }
    for (int i = 0; i < 4; i++) lv_obj_set_hidden(s_fs_badges[i], true);
    int badge_cols = n >= 2 ? 2 : n;
    int badge_rows = (n + 1) / 2;
    int32_t badges_w = badge_cols ? badge_cols * BADGE_W + (badge_cols - 1) * BADGE_GAP : 0;
    (void)badge_rows;

    /* Everything top-aligned with the nav buttons, so nothing jumps between one- and two-line names. */
    const int32_t top = ROW_Y + (ROW_H - NAV_H) / 2;
    const int32_t max_h = ROW_Y + ROW_H - top - 2;
    lv_obj_update_layout(s_preset_label);
    int32_t label_w = lv_obj_get_width(s_preset_label);
    int32_t col_w = label_w > badges_w ? label_w : badges_w;
    int32_t col_x = EDGE_X + NAV_W + 4;
    int32_t x = col_x + col_w + (col_w ? 6 : 0);
    int32_t w = SCREEN_W - EDGE_X - NAV_W - 4 - x;
    const char *text = lv_label_get_text(s_preset_name);
    const lv_font_t *font = fit_font(text, w, max_h);
    lv_obj_set_style_text_font(s_preset_name, font, 0);
    lv_obj_set_width(s_preset_name, w);
    lv_obj_set_pos(s_preset_name, x, top);

    /* Label at the top of the column, badges under it. */
    int32_t label_h = lv_font_get_line_height(&lv_font_montserrat_24);
    lv_obj_set_pos(s_preset_label, col_x, top + 2);
    for (int k = 0; k < n; k++) {
        int32_t bx = col_x + (k % 2) * (BADGE_W + BADGE_GAP);
        int32_t by = top + 2 + label_h + 2 + (k / 2) * (BADGE_H + BADGE_GAP);
        lv_obj_set_pos(s_fs_badges[shown[k]], bx, by);
        lv_obj_set_hidden(s_fs_badges[shown[k]], false);
    }
}

void nano_ui_set_footswitches(const uint8_t fs[4])
{
    memcpy(s_footswitch, fs, 4);
    s_footswitch_known = true;
    layout_preset_row();
}

void nano_ui_set_preset(uint8_t index, const nano_metadata_t *meta)
{
    s_preset = index;
    char label[8];
    nano_preset_label(index, s_per_bank, label, sizeof(label));
    lv_label_set_text(s_preset_label, label);
    lv_obj_set_style_text_color(s_preset_label, lv_color_hex(SLOT_COLORS[(index % s_per_bank) & 7]), 0);
    const char *name = (meta && index < NANO_PRESET_COUNT) ? meta->presets[index].name : "";
    char fallback[24];
    if (!name[0]) {
        snprintf(fallback, sizeof(fallback), "Preset %u", (unsigned)index + 1);
        name = fallback;
    }
    lv_label_set_text(s_preset_name, name);
    layout_preset_row();
}

static void set_line(lv_obj_t *dot, lv_obj_t *label, const char *name, bool on, const char *empty)
{
    lv_label_set_text(label, name && name[0] ? name : empty);
    lv_obj_set_style_bg_color(dot, lv_color_hex(on ? C_ON : C_DIM), 0);
    lv_obj_set_style_text_color(label, lv_color_hex(on ? C_TEXT : C_OFF_TEXT), 0);
}

void nano_ui_set_state(const nano_state_t *st, const nano_metadata_t *meta)
{
    memcpy(s_footswitch, st->footswitch, 4);
    s_footswitch_known = true;
    nano_ui_set_preset(st->active_preset, meta);
    if ((!meta || !meta->presets[st->active_preset].name[0]) && st->capture_name[0]) {
        /* No cached name yet: the capture name is the most recognisable thing we have. */
        lv_label_set_text(s_preset_name, st->capture_name);
        layout_preset_row();
    }
    set_line(s_capture_dot, s_capture, st->capture_name, st->capture_on, "No capture");
    set_line(s_ir_dot, s_ir, st->ir_short_name, st->cab_on, "No IR");
    s_gate_on = st->gate_on;
    lv_obj_set_style_bg_color(s_gate, lv_color_hex(st->gate_on ? nano_category_color(NANO_CAT_UTILITY) : C_OFF), 0);
    lv_obj_set_style_text_color(lv_obj_get_child(s_gate, 0), lv_color_hex(st->gate_on ? C_FX_TEXT : C_TEXT), 0);
    if (st->tempo_bpm > 0) {
        char t[16];
        snprintf(t, sizeof(t), "%d BPM", (int)(st->tempo_bpm + 0.5f));
        lv_label_set_text(s_tempo, t);
    } else {
        lv_label_set_text(s_tempo, "");
    }
    for (int i = 0; i < NANO_FX_SLOT_COUNT; i++) {
        const nano_fx_slot_t *fx = &st->fx[i];
        bool present = fx->id[0] != 0;
        bool on = present && st->has_bypass && st->fx_on[i];
        s_tile_present[i] = present;
        s_tile_on[i] = on;
        nano_category_t cat = fx->model ? fx->model->category : NANO_CAT_UNKNOWN;
        uint32_t color = nano_category_color(cat);
        const char *name = fx->model ? fx->model->name : (present ? fx->id : "");
        lv_label_set_text(s_tile_names[i], name);
        lv_obj_set_style_text_font(s_tile_names[i], tile_font(name, TILE_W - 4), 0);
        if (!present) {
            lv_obj_set_style_bg_color(s_tiles[i], lv_color_hex(C_BG), 0);
            lv_obj_set_style_border_color(s_tiles[i], lv_color_hex(0x262D37), 0);
            lv_obj_set_style_text_color(s_tile_names[i], lv_color_hex(C_DIM), 0);
        } else if (on) {
            lv_obj_set_style_bg_color(s_tiles[i], lv_color_hex(color), 0);
            lv_obj_set_style_border_color(s_tiles[i], lv_color_hex(color), 0);
            lv_obj_set_style_text_color(s_tile_names[i], lv_color_hex(nano_category_light_text(cat) ? C_TEXT : C_FX_TEXT), 0);
        } else {
            lv_obj_set_style_bg_color(s_tiles[i], lv_color_hex(C_OFF), 0);
            lv_obj_set_style_border_color(s_tiles[i], lv_color_mix(lv_color_hex(color), lv_color_hex(C_OFF), 140), 0);
            lv_obj_set_style_text_color(s_tile_names[i], lv_color_hex(C_TEXT), 0);
        }
    }
}

void nano_ui_set_stale(bool stale)
{
    if (stale) {
        /* No live state: a neutral placeholder instead of a preset the pedal may not be on. */
        s_footswitch_known = false;
        lv_label_set_text(s_preset_label, "");
        lv_label_set_text(s_preset_name, "Preset name");
        layout_preset_row();
    }
    lv_opa_t opa = stale ? LV_OPA_50 : LV_OPA_COVER;
    lv_obj_set_style_opa(s_preset_label, opa, 0);
    lv_obj_set_style_opa(s_preset_name, opa, 0);
    lv_obj_set_style_opa(s_capture, opa, 0);
    lv_obj_set_style_opa(s_ir, opa, 0);
    lv_obj_set_style_opa(s_gate, opa, 0);
    for (int i = 0; i < 4; i++) lv_obj_set_style_opa(s_fs_badges[i], opa, 0);
    for (int i = 0; i < NANO_FX_SLOT_COUNT; i++) lv_obj_set_style_opa(s_tiles[i], opa, 0);
}

void nano_ui_set_tuner(const char *note, float cents, bool in_tune)
{
    if (!note || !note[0]) {
        lv_label_set_text(s_tuner_note, "-");
        lv_obj_set_style_text_color(s_tuner_note, lv_color_hex(C_DIM), 0);
        lv_label_set_text(s_tuner_cents, "");
        lv_label_set_text(s_tuner_verdict, "Play a note");
        lv_obj_set_hidden(s_tuner_bar, true);
        return;
    }
    lv_label_set_text(s_tuner_note, note);
    uint32_t c = in_tune ? C_ON : (cents < -10 || cents > 10) ? C_ERROR : C_WARN;
    lv_obj_set_style_text_color(s_tuner_note, lv_color_hex(c), 0);
    lv_obj_set_style_bg_color(s_tuner_bar, lv_color_hex(c), 0);
    lv_obj_set_hidden(s_tuner_bar, false);
    float clamped = cents < -50 ? -50 : cents > 50 ? 50 : cents;
    int32_t px = (int32_t)(clamped * (SCREEN_W / 2 - 26) / 50.0f);
    lv_obj_set_pos(s_tuner_bar, SCREEN_W / 2 - 6 + px, 108);
    char t[24];
    snprintf(t, sizeof(t), "%+d ct", (int)(cents < 0 ? cents - 0.5f : cents + 0.5f));
    lv_label_set_text(s_tuner_cents, t);
    lv_label_set_text(s_tuner_verdict, in_tune ? "In tune" : cents < 0 ? "Flat" : "Sharp");
}
