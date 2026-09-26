#include "nano_ui.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "nano_models.h"

/* NanoGig palette (src/ui/styles.css). */
#define C_BG 0x07090C
#define C_PANEL 0x11151B
#define C_TEXT 0xF4F6F8
#define C_MUTED 0x8D97A5
#define C_DIM 0x4D5661
#define C_ON 0x2DD4A0
#define C_OFF 0x2A313B
#define C_OFF_TEXT 0x6B7583
#define C_WARN 0xFFB454
#define C_ERROR 0xFF5C6C
#define C_FX_TEXT 0x0B0D10

/* Slot colours for the preset label (1A red, 1B orange, 1C green, 1D cyan ...). */
static const uint32_t SLOT_COLORS[8] = { 0xFF5C5C, 0xFFB454, 0x4CF06A, 0x00F0D8, 0x3D9BFF, 0xA78BFA, 0xF050C8, 0xF4F6F8 };
#define PRESETS_PER_BANK 4 /* Mvave Chocolate layout, like the web app's default */

static nano_ui_callbacks_t s_cb;
static lv_obj_t *s_status_dot, *s_status, *s_tempo, *s_gate;
static lv_obj_t *s_preset_label, *s_preset_name;
static lv_obj_t *s_capture_dot, *s_capture, *s_ir_dot, *s_ir;
static lv_obj_t *s_tiles[NANO_FX_SLOT_COUNT], *s_tile_names[NANO_FX_SLOT_COUNT];
static lv_obj_t *s_root;
static uint8_t s_preset = 0;
static bool s_tile_present[NANO_FX_SLOT_COUNT];
static bool s_tile_on[NANO_FX_SLOT_COUNT];

static const lv_font_t *fit_font(const char *text, int32_t max_w)
{
    static const lv_font_t *const candidates[] = { &lv_font_montserrat_40, &lv_font_montserrat_28, &lv_font_montserrat_20 };
    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
        lv_point_t size;
        lv_text_get_size(&size, text, candidates[i], 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        if (size.x <= max_w) return candidates[i];
    }
    return &lv_font_montserrat_20; /* wraps onto two lines */
}

static lv_point_t s_press_point;

/* Any press: remember where it started (the release sample of a resistive panel drifts) and log it. */
static void on_screen_pressed(lv_event_t *e)
{
    lv_indev_t *indev = lv_event_get_indev(e);
    if (!indev) return;
    lv_indev_get_point(indev, &s_press_point);
    printf("touch press x=%d y=%d\n", (int)s_press_point.x, (int)s_press_point.y);
}

static void on_name_clicked(lv_event_t *e)
{
    (void)e;
    int32_t w = lv_display_get_horizontal_resolution(NULL);
    if (s_press_point.x < w / 3) {
        if (s_cb.on_prev_preset) s_cb.on_prev_preset();
    } else if (s_press_point.x > w * 2 / 3) {
        if (s_cb.on_next_preset) s_cb.on_next_preset();
    }
}

static void on_tile_clicked(lv_event_t *e)
{
    uint8_t slot = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
    if (slot >= NANO_FX_SLOT_COUNT || !s_tile_present[slot]) return;
    if (s_cb.on_toggle_fx) s_cb.on_toggle_fx(slot, s_tile_on[slot]);
}

static lv_obj_t *make_label(lv_obj_t *parent, const lv_font_t *font, uint32_t color)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_label_set_text(l, "");
    return l;
}

static lv_obj_t *make_dot(lv_obj_t *parent, int32_t x, int32_t y, int32_t d)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, d, d);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(o, lv_color_hex(C_DIM), 0);
    return o;
}

void nano_ui_create(lv_display_t *disp, const nano_ui_callbacks_t *cb)
{
    s_cb = *cb;
    lv_obj_t *scr = lv_display_get_screen_active(disp);
    lv_obj_set_style_bg_color(scr, lv_color_hex(C_BG), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_scrollable(scr, false);
    lv_obj_add_event_cb(scr, on_screen_pressed, LV_EVENT_PRESSED, NULL);
    s_root = scr;

    /* Top bar: status dot + text, tempo + gate in the middle, preset label right. */
    s_status_dot = make_dot(scr, 8, 9, 8);
    s_status = make_label(scr, &lv_font_montserrat_12, C_MUTED);
    lv_obj_set_pos(s_status, 22, 6);
    lv_obj_set_width(s_status, 150);
    lv_label_set_long_mode(s_status, LV_LABEL_LONG_DOT);

    s_tempo = make_label(scr, &lv_font_montserrat_12, C_MUTED);
    lv_obj_set_pos(s_tempo, 176, 6);
    s_gate = make_label(scr, &lv_font_montserrat_12, C_DIM);
    lv_obj_set_pos(s_gate, 226, 6);
    lv_label_set_text(s_gate, "GATE");

    s_preset_label = make_label(scr, &lv_font_montserrat_28, C_TEXT);
    lv_obj_align(s_preset_label, LV_ALIGN_TOP_RIGHT, -8, 0);

    /* Preset name: fills the middle, tappable at the edges. */
    s_preset_name = make_label(scr, &lv_font_montserrat_40, C_TEXT);
    lv_obj_set_pos(s_preset_name, 8, 34);
    lv_obj_set_size(s_preset_name, 304, 92);
    lv_obj_set_style_text_align(s_preset_name, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s_preset_name, LV_LABEL_LONG_WRAP);
    lv_obj_set_clickable(s_preset_name, true);
    lv_obj_set_ext_click_area(s_preset_name, 8);
    lv_obj_add_event_cb(s_preset_name, on_screen_pressed, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(s_preset_name, on_name_clicked, LV_EVENT_CLICKED, NULL);
    lv_label_set_text(s_preset_name, "NanoGig");

    /* Capture / IR lines. */
    s_capture_dot = make_dot(scr, 10, 135, 10);
    s_capture = make_label(scr, &lv_font_montserrat_14, C_TEXT);
    lv_obj_set_pos(s_capture, 28, 132);
    lv_obj_set_width(s_capture, 284);
    lv_label_set_long_mode(s_capture, LV_LABEL_LONG_DOT);

    s_ir_dot = make_dot(scr, 10, 155, 10);
    s_ir = make_label(scr, &lv_font_montserrat_14, C_MUTED);
    lv_obj_set_pos(s_ir, 28, 152);
    lv_obj_set_width(s_ir, 284);
    lv_label_set_long_mode(s_ir, LV_LABEL_LONG_DOT);

    /* Five FX tiles: pre1 pre2 | post1 post2 post3. */
    const int32_t tile_w = 60, tile_h = 60, gap = 4, y = 176;
    int32_t x = (320 - (tile_w * NANO_FX_SLOT_COUNT + gap * (NANO_FX_SLOT_COUNT - 1) + 6)) / 2; /* +6: wider gap between pre and post */
    for (int i = 0; i < NANO_FX_SLOT_COUNT; i++) {
        lv_obj_t *t = lv_obj_create(scr);
        lv_obj_remove_style_all(t);
        lv_obj_set_size(t, tile_w, tile_h);
        lv_obj_set_pos(t, x + i * (tile_w + gap) + (i >= 2 ? 6 : 0), y);
        lv_obj_set_style_radius(t, 10, 0);
        lv_obj_set_style_bg_opa(t, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(t, lv_color_hex(C_PANEL), 0);
        lv_obj_set_style_border_width(t, 3, 0);
        lv_obj_set_style_border_side(t, LV_BORDER_SIDE_TOP, 0);
        lv_obj_set_style_border_color(t, lv_color_hex(C_DIM), 0);
        lv_obj_set_style_pad_all(t, 4, 0);
        lv_obj_set_scrollable(t, false);
        lv_obj_set_clickable(t, true);
        lv_obj_set_ext_click_area(t, 6);
        lv_obj_add_event_cb(t, on_screen_pressed, LV_EVENT_PRESSED, NULL);
        lv_obj_add_event_cb(t, on_tile_clicked, LV_EVENT_CLICKED, (void *)(uintptr_t)i);
        lv_obj_t *name = make_label(t, &lv_font_montserrat_12, C_DIM);
        lv_obj_set_width(name, tile_w - 8);
        lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_long_mode(name, LV_LABEL_LONG_WRAP);
        lv_obj_center(name);
        lv_label_set_text(name, i < 2 ? "PRE" : "POST");
        s_tiles[i] = t;
        s_tile_names[i] = name;
    }
}

void nano_ui_set_status(const char *text, bool connected)
{
    lv_label_set_text(s_status, text);
    lv_obj_set_style_bg_color(s_status_dot, lv_color_hex(connected ? C_ON : C_WARN), 0);
}

static void set_preset_label(uint8_t index)
{
    char label[8];
    nano_preset_label(index, PRESETS_PER_BANK, label, sizeof(label));
    lv_label_set_text(s_preset_label, label);
    lv_obj_set_style_text_color(s_preset_label, lv_color_hex(SLOT_COLORS[(index % PRESETS_PER_BANK) & 7]), 0);
    lv_obj_align(s_preset_label, LV_ALIGN_TOP_RIGHT, -8, 0);
}

static void set_preset_name(const char *name)
{
    const char *text = name && name[0] ? name : "Untitled";
    lv_obj_set_style_text_font(s_preset_name, fit_font(text, 300), 0);
    lv_label_set_text(s_preset_name, text);
}

void nano_ui_set_preset(uint8_t index, const nano_metadata_t *meta)
{
    s_preset = index;
    set_preset_label(index);
    const char *name = (meta && index < NANO_PRESET_COUNT) ? meta->presets[index].name : "";
    if (name[0]) {
        set_preset_name(name);
    } else {
        char fallback[24];
        snprintf(fallback, sizeof(fallback), "Preset %u", (unsigned)index + 1);
        set_preset_name(fallback);
    }
}

static void set_line(lv_obj_t *dot, lv_obj_t *label, const char *name, bool on, const char *empty)
{
    lv_label_set_text(label, name && name[0] ? name : empty);
    lv_obj_set_style_bg_color(dot, lv_color_hex(on ? C_ON : C_DIM), 0);
    lv_obj_set_style_text_color(label, lv_color_hex(on ? C_TEXT : C_OFF_TEXT), 0);
}

void nano_ui_set_state(const nano_state_t *st, const nano_metadata_t *meta)
{
    nano_ui_set_preset(st->active_preset, meta);
    if (!meta || !meta->presets[st->active_preset].name[0]) {
        /* No cached name yet: the capture name is the most recognisable thing we have. */
        if (st->capture_name[0]) set_preset_name(st->capture_name);
    }
    set_line(s_capture_dot, s_capture, st->capture_name, st->capture_on, "No capture");
    set_line(s_ir_dot, s_ir, st->ir_short_name, st->cab_on, "No IR");
    lv_obj_set_style_text_color(s_gate, lv_color_hex(st->gate_on ? C_MUTED : C_DIM), 0);
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
            lv_obj_set_style_text_color(s_tile_names[i], lv_color_hex(C_OFF_TEXT), 0);
        }
    }
}

void nano_ui_set_stale(bool stale)
{
    lv_obj_set_style_opa(s_preset_name, stale ? LV_OPA_50 : LV_OPA_COVER, 0);
    lv_obj_set_style_opa(s_capture, stale ? LV_OPA_50 : LV_OPA_COVER, 0);
    lv_obj_set_style_opa(s_ir, stale ? LV_OPA_50 : LV_OPA_COVER, 0);
    for (int i = 0; i < NANO_FX_SLOT_COUNT; i++) lv_obj_set_style_opa(s_tiles[i], stale ? LV_OPA_50 : LV_OPA_COVER, 0);
}
