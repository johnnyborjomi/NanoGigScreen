#include "nano_ui.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "nano_models.h"
#include "ui_common.h"
#include "ui_text_edit.h"
#include "ui_value_ctrl.h"

/* Slot colours for the preset label (1A red, 1B orange, 1C green, 1D cyan ...). */
static const uint32_t SLOT_COLORS[8] = { 0xFF5C5C, 0xFFB454, 0x4CF06A, 0x00F0D8, 0x3D9BFF, 0xA78BFA, 0xF050C8, 0xF4F6F8 };

#define TOP_H 22
#define ROW_Y 24
#define ROW_H 94
#define NAV_W 36          /* prev / next buttons: narrower and shorter than the row, centred on it */
#define NAV_H 66
#define LINES_Y 120       /* capture / IR / gate row, pulled up to give the tiles room for the category tag */
#define LINES_H 40
#define EDGE_X 2          /* left edge shared by the prev button, the gate button and the first tile */
#define EXP_BAR_W 3       /* expression pedal position: a thin bar in the right gutter, from the preset row to the tiles */
#define EXP_BAR_X (SCREEN_W - EXP_BAR_W)
#define RIGHT_X (EXP_BAR_X - 1) /* right edge of the next button and the last tile */
#define EXP_BAR_Y ROW_Y
#define EXP_BAR_H (TILE_Y + TILE_H - ROW_Y)
#define TILE_EXP_H 6      /* per-tile expression track at the bottom of an assigned FX tile */
#define TILE_EXP_INSET 4
#define SETTING_X (EDGE_X + 36 + 8) /* right of the pager column (PAGER_W) */
#define SETTING_RIGHT (SCREEN_W - 12 - SETTING_X)
#define SETTING_ROW_H 34
#define GATE_W 40
#define GATE_H 26
#define TILE_Y 164
#define TILE_W 60
#define TILE_H 74
#define TILE_TAG_Y 1      /* category tag: right under the top border, same spot on and off */
#define TILE_NAME_DY 6    /* name centred in the space below the tag */

static nano_ui_callbacks_t s_cb;
static lv_obj_t *s_scr;
static lv_obj_t *s_main, *s_menu, *s_settings, *s_tuner, *s_tempo_view, *s_connect;
static lv_obj_t *s_capture_view; /* Capture / IR tabs: built on open, freed on close, like the presets list */
static lv_obj_t *s_tab_btn[2], *s_tab_page; /* header tabs, and the content of the one showing (rebuilt on a switch) */
static int s_tab_built = -1;                 /* 0 = capture, 1 = IR, -1 = none */
static lv_obj_t *s_presets; /* built on open, freed on close: ~10 KB of heap the gig needs more */
static lv_obj_t *s_rename_view; /* built on open, freed on close (the keyboard) */
static ui_text_edit_t *s_rename_edit; /* freed with the page */
static uint8_t s_rename_idx;
static nano_view_t s_rename_from = NANO_VIEW_MAIN; /* where "<" goes back to */
static lv_obj_t *s_update; /* built on first open: the keyboard and the network list cost heap the gig never needs */
static lv_obj_t *s_tempo_big, *s_tempo_hint;
static float s_tempo_bpm;
static bool s_tempo_tapping;
static nano_view_t s_view = NANO_VIEW_MAIN;
/* Where "close" lands: the main view once a state dump arrived, the connect page otherwise. */
static nano_view_t s_base_view = NANO_VIEW_CONNECT;

/* connect page */
static lv_obj_t *s_connect_title, *s_connect_status, *s_connect_dot, *s_connect_pair, *s_connect_btn, *s_connect_free;

/* main view */
static lv_obj_t *s_status_dot, *s_status, *s_tempo, *s_gate, *s_list_btn;
static bool s_gate_on;
static lv_obj_t *s_preset_label, *s_preset_name, *s_prev, *s_next;
static lv_obj_t *s_capture_dot, *s_capture, *s_ir_dot, *s_ir;
/* The capture as the last state dump showed it (the capture page draws from these). */
static char s_cap_name[NANO_NAME_CAP];
static bool s_cap_on;
static int s_cap_volume = -1;       /* raw 0..255, -1 = unknown */
static int s_cap_preset = -1;      /* preset the capture belongs to */
static lv_obj_t *s_cap_dot, *s_cap_name_l;
static ui_value_ctrl_t *s_cap_vol;  /* the volume control while the page is open (freed with it) */
/* The IR as the last state dump showed it, and its settings as the pedal last reported them. */
static char s_ir_name[NANO_NAME_CAP];
static bool s_ir_on;
static nano_cab_settings_t s_ir_set; /* the pedal's last answer (values, microphone, position) */
static bool s_ir_known;            /* s_ir holds the pedal's answer for s_ir_owner */
static int s_ir_owner = -1;        /* preset the settings belong to */
static lv_obj_t *s_ir_dot_l, *s_ir_name_l, *s_ir_hint, *s_ir_phase_btn, *s_ir_page;
#define IR_MIC_BUTTONS 6 /* Cortex Cloud offers five */
static lv_obj_t *s_ir_mic_btn[IR_MIC_BUTTONS], *s_ir_pos_btn[NANO_CAB_POSITIONS], *s_ir_mic_note;
static ui_value_ctrl_t *s_ir_ctrl[NANO_CAB_PARAMS]; /* while the IR tab exists (freed with it) */
static lv_obj_t *s_tiles[NANO_FX_SLOT_COUNT], *s_tile_names[NANO_FX_SLOT_COUNT], *s_tile_tags[NANO_FX_SLOT_COUNT];
static bool s_tile_present[NANO_FX_SLOT_COUNT];
static bool s_tile_on[NANO_FX_SLOT_COUNT];
static uint32_t s_tile_color[NANO_FX_SLOT_COUNT];
/* Expression pedal: side bar + per-tile tracks (band = assigned range, fill = the value the pedal produces). */
static lv_obj_t *s_exp_bar, *s_exp_fill;
static lv_obj_t *s_tile_exp[NANO_FX_SLOT_COUNT], *s_tile_exp_band[NANO_FX_SLOT_COUNT], *s_tile_exp_fill[NANO_FX_SLOT_COUNT];
static int s_exp_pos = -1;          /* -1 = unknown (drawn at the heel) */
static bool s_exp_show = true;      /* setting: show the indicators at all */
static bool s_exp_assign_valid, s_exp_values_valid;
static nano_exp_assignments_t s_exp_assign;
static nano_exp_values_t s_exp_values;
static uint8_t s_preset;
static const nano_metadata_t *s_meta; /* the app's cache, as last passed with a preset (NULL = none) */
static uint8_t s_per_bank = 4;
static nano_label_style_t s_label_style = NANO_LABEL_NUMBER_LETTER;
static lv_obj_t *s_mute_badge;
static bool s_outputs_muted;
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

/* menu / settings / tuner */
static lv_obj_t *s_link_btn_label, *s_bank_value, *s_style_seg[3], *s_style_hint, *s_mute_toggle, *s_mute_knob, *s_exp_toggle, *s_exp_knob;
/* settings page 2: display */
static lv_obj_t *s_rot_seg[2], *s_bright_value;
static bool s_rot180;
static uint8_t s_brightness = 10;
#define BRIGHTNESS_MIN 1
#define BRIGHTNESS_MAX 10


/* settings page 3 + update view */
static char s_fw_version[32] = "unknown";
static char s_wifi_ssid[33];
static lv_obj_t *s_fw_value;
static lv_obj_t *s_upd_main, *s_upd_version, *s_upd_wifi, *s_upd_text, *s_upd_sub, *s_upd_bar, *s_upd_action, *s_upd_close;
static lv_obj_t *s_upd_nets, *s_upd_net_list, *s_upd_back, *s_upd_title, *s_upd_nets_title;
static bool s_upd_scanning;
static lv_obj_t *s_upd_pass, *s_upd_pass_title, *s_upd_pass_ta, *s_upd_kb;
static nano_update_state_t s_upd_state = NANO_UPDATE_BUSY;
static nano_ui_network_t s_upd_networks[10];
static int s_upd_network_count;
static char s_upd_pick[33];  /* network whose password is being typed */

static lv_obj_t *s_tuner_note, *s_tuner_cents, *s_tuner_bar, *s_tuner_verdict, *s_tuner_mute;
static bool s_tuner_muted;

/* ---- helpers -------------------------------------------------------------- */

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

/* Tile names wrap on spaces; a single word wider than the tile, or a name that needs more than
 * three lines ("Solid State Comp (M)"), drops to the 10 px font. */
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
    lv_point_t size;
    lv_text_get_size(&size, text, &montserrat_medium_12, 0, 0, max_w, LV_TEXT_FLAG_NONE);
    if (size.y > 3 * lv_font_get_line_height(&montserrat_medium_12)) return &montserrat_medium_10;
    return &montserrat_medium_12;
}

/* ---- pager: a left column with up / down buttons and "Page" over "n/m" between them ----
 * Pages are either child boxes shown one at a time (pager_add_page) or virtual: a count and an
 * on_show callback that refills one box (pager_set_count, the presets list). */

#define PAGER_W 36
#define PAGER_BTN_H 40
#define PAGER_MAX_PAGES 4

typedef struct {
    lv_obj_t *up, *down, *label;
    lv_obj_t *pages[PAGER_MAX_PAGES];
    int count, current;
    int32_t mid_y;
    const char *unit;          /* "Page", "Bank" */
    void (*on_show)(int idx);  /* virtual pages */
    bool wrap;                 /* past the last page lands on the first (and back) */
} pager_t;

static void pager_show(pager_t *p, int idx)
{
    if (idx < 0 || idx >= p->count) return;
    p->current = idx;
    for (int i = 0; i < p->count && i < PAGER_MAX_PAGES; i++) if (p->pages[i]) lv_obj_set_hidden(p->pages[i], i != idx);
    /* A single page needs no column (the page keeps whatever x it was given). */
    bool solo = p->count <= 1;
    lv_obj_set_hidden(p->up, solo);
    lv_obj_set_hidden(p->down, solo);
    lv_obj_set_hidden(p->label, solo);
    char t[24];
    snprintf(t, sizeof(t), "%s\n%u/%u", p->unit, (unsigned)(idx + 1) & 0xff, (unsigned)p->count & 0xff);
    lv_label_set_text(p->label, t);
    /* Upright, centred on the column: a rotated label needs a 32-bit layer per redraw, 4-9 KB of heap at a
     * time on every page change (the IR page's heap dip to 10 KB, 2026-10-09). */
    lv_obj_update_layout(p->label);
    lv_obj_set_y(p->label, p->mid_y - lv_obj_get_height(p->label) / 2);
    /* Ends of the range: dim the arrow that goes nowhere (a wrapping pager has none). */
    lv_obj_set_style_opa(p->up, idx == 0 && !p->wrap ? LV_OPA_30 : LV_OPA_COVER, 0);
    lv_obj_set_style_opa(p->down, idx == p->count - 1 && !p->wrap ? LV_OPA_30 : LV_OPA_COVER, 0);
    if (p->on_show) p->on_show(idx);
}

static void on_pager_step(lv_event_t *e)
{
    pager_t *p = (pager_t *)lv_event_get_user_data(e);
    int delta = lv_event_get_target_obj(e) == p->up ? -1 : 1;
    int idx = p->current + delta;
    if (p->wrap && p->count > 0) idx = (idx + p->count) % p->count;
    pager_show(p, idx);
}

/* Column at the left edge from y to y + h; pages are added with pager_add_page and shown one at a time. */
static void pager_create(pager_t *p, lv_obj_t *parent, int32_t y, int32_t h, const char *unit)
{
    memset(p, 0, sizeof(*p));
    p->unit = unit;
    p->mid_y = y + h / 2;
    p->up = ui_button(parent, EDGE_X, y, PAGER_W, PAGER_BTN_H, LV_SYMBOL_UP, &lv_font_montserrat_14, C_PANEL, C_MUTED, on_pager_step, p);
    lv_obj_add_event_cb(p->up, ui_on_pressed, LV_EVENT_PRESSED, NULL);
    p->down = ui_button(parent, EDGE_X, y + h - PAGER_BTN_H, PAGER_W, PAGER_BTN_H, LV_SYMBOL_DOWN, &lv_font_montserrat_14, C_PANEL, C_MUTED, on_pager_step, p);
    lv_obj_add_event_cb(p->down, ui_on_pressed, LV_EVENT_PRESSED, NULL);
    p->label = ui_label(parent, &montserrat_medium_12, C_MUTED);
    lv_label_set_text(p->label, "");
    lv_obj_set_x(p->label, EDGE_X);
    lv_obj_set_width(p->label, PAGER_W);
    lv_obj_set_style_text_align(p->label, LV_TEXT_ALIGN_CENTER, 0);
}

static lv_obj_t *pager_add_page(pager_t *p, lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h)
{
    if (p->count >= PAGER_MAX_PAGES) return NULL;
    lv_obj_t *page = ui_box(parent, x, y, w, h, C_BG);
    p->pages[p->count++] = page;
    pager_show(p, p->current);
    return page;
}

/* Virtual pages (no boxes): `count` of them, on_show draws the current one. */
static void pager_set_count(pager_t *p, int count, int current)
{
    p->count = count;
    pager_show(p, current < count ? current : count - 1);
}

static pager_t s_settings_pager;

/* ---- callbacks ------------------------------------------------------------ */

static void on_prev(lv_event_t *e) { (void)e; if (s_cb.on_prev_preset) s_cb.on_prev_preset(); }
static void on_next(lv_event_t *e) { (void)e; if (s_cb.on_next_preset) s_cb.on_next_preset(); }
static void on_menu(lv_event_t *e) { (void)e; nano_ui_show(NANO_VIEW_MENU); }
static void on_close(lv_event_t *e) { (void)e; nano_ui_show(s_base_view); }
static void on_open_settings(lv_event_t *e) { (void)e; nano_ui_show(NANO_VIEW_SETTINGS); }
static void on_open_tuner(lv_event_t *e) { (void)e; nano_ui_show(NANO_VIEW_TUNER); }
static void on_back_to_menu(lv_event_t *e) { (void)e; nano_ui_show(NANO_VIEW_MENU); }
static void on_open_tempo(lv_event_t *e) { (void)e; nano_ui_show(NANO_VIEW_TEMPO); }
static void on_open_update(lv_event_t *e) { (void)e; nano_ui_show(NANO_VIEW_UPDATE); }
static void on_open_presets(lv_event_t *e) { (void)e; nano_ui_show(NANO_VIEW_PRESETS); }
static void on_open_capture(lv_event_t *e) { (void)e; nano_ui_show(NANO_VIEW_CAPTURE); }
static void on_open_ir(lv_event_t *e) { (void)e; nano_ui_show(NANO_VIEW_IR); }
static void on_tempo_step(lv_event_t *e)
{
    int delta = (int)(intptr_t)lv_event_get_user_data(e);
    if (s_cb.on_tempo_delta) s_cb.on_tempo_delta(delta);
}

static void on_link_toggle(lv_event_t *e)
{
    (void)e;
    s_link_enabled = !s_link_enabled;
    nano_ui_set_link_enabled(s_link_enabled);
    if (s_cb.on_link) s_cb.on_link(s_link_enabled);
    nano_ui_show(s_base_view);
}

/* The connect page's button: enable the link (the app scans again). */
static void on_connect_clicked(lv_event_t *e)
{
    (void)e;
    if (s_link_enabled) return;
    s_link_enabled = true;
    nano_ui_set_link_enabled(true);
    if (s_cb.on_link) s_cb.on_link(true);
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

static void refresh_style_seg(void)
{
    static const char *const HINTS[3] = { "Mvave Chocolate", "Other MIDI controllers", "As on the Nano Cortex" };
    for (int i = 0; i < 3; i++) {
        bool sel = (int)s_label_style == i;
        lv_obj_set_style_bg_color(s_style_seg[i], lv_color_hex(sel ? C_ACCENT : C_PANEL), 0);
        lv_obj_set_style_text_color(lv_obj_get_child(s_style_seg[i], 0), lv_color_hex(sel ? C_FX_TEXT : C_TEXT), 0);
    }
    lv_label_set_text(s_style_hint, HINTS[s_label_style < 3 ? s_label_style : 0]);
}

static void on_style_clicked(lv_event_t *e)
{
    uint8_t style = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
    if (style > NANO_LABEL_NUMERIC || style == s_label_style) return;
    s_label_style = (nano_label_style_t)style;
    refresh_style_seg();
    if (s_cb.on_label_style) s_cb.on_label_style(style);
}

/* iPhone-style switch: a 52 x 28 pill with a round knob; `on_color` fills it while on. */
static void set_toggle(lv_obj_t *pill, lv_obj_t *knob, bool on, uint32_t on_color)
{
    lv_obj_set_style_bg_color(pill, lv_color_hex(on ? on_color : C_OFF), 0);
    lv_obj_align(knob, on ? LV_ALIGN_RIGHT_MID : LV_ALIGN_LEFT_MID, on ? -3 : 3, 0);
}
static lv_obj_t *make_toggle(lv_obj_t *parent, int32_t y, lv_event_cb_t cb, lv_obj_t **knob_out)
{
    lv_obj_t *pill = ui_box(parent, SETTING_RIGHT - 52, y + (SETTING_ROW_H - 28) / 2, 52, 28, C_OFF);
    lv_obj_set_style_radius(pill, 14, 0);
    lv_obj_set_clickable(pill, true);
    lv_obj_set_ext_click_area(pill, 10);
    lv_obj_add_event_cb(pill, ui_on_pressed, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(pill, cb, LV_EVENT_CLICKED, NULL);
    *knob_out = ui_box(pill, 0, 0, 22, 22, C_TEXT);
    lv_obj_set_style_radius(*knob_out, LV_RADIUS_CIRCLE, 0);
    return pill;
}
static void refresh_mute(void)
{
    lv_obj_set_hidden(s_mute_badge, !s_outputs_muted);
    set_toggle(s_mute_toggle, s_mute_knob, s_outputs_muted, C_ERROR);
}
static void refresh_exp_toggle(void) { set_toggle(s_exp_toggle, s_exp_knob, s_exp_show, C_WARN); }
static void refresh_expression(void);
static void on_exp_toggle_clicked(lv_event_t *e)
{
    (void)e;
    s_exp_show = !s_exp_show;
    refresh_exp_toggle();
    refresh_expression();
    if (s_cb.on_expression_show) s_cb.on_expression_show(s_exp_show);
}

static void on_mute_toggle_clicked(lv_event_t *e)
{
    (void)e;
    s_outputs_muted = !s_outputs_muted; /* optimistic; the pedal's settings reply confirms */
    refresh_mute();
    if (s_cb.on_outputs_mute) s_cb.on_outputs_mute(s_outputs_muted);
}

static void refresh_rot_seg(void)
{
    for (int i = 0; i < 2; i++) {
        bool sel = (int)s_rot180 == i;
        lv_obj_set_style_bg_color(s_rot_seg[i], lv_color_hex(sel ? C_ACCENT : C_PANEL), 0);
        lv_obj_set_style_text_color(lv_obj_get_child(s_rot_seg[i], 0), lv_color_hex(sel ? C_FX_TEXT : C_TEXT), 0);
    }
}
static void on_rot_clicked(lv_event_t *e)
{
    bool rot = (uintptr_t)lv_event_get_user_data(e) != 0;
    if (rot == s_rot180) return;
    s_rot180 = rot;
    refresh_rot_seg();
    if (s_cb.on_rotation) s_cb.on_rotation(rot);
}
static void set_brightness_text(void)
{
    char t[4];
    snprintf(t, sizeof(t), "%u", (unsigned)s_brightness);
    lv_label_set_text(s_bright_value, t);
}
static void on_brightness_step(lv_event_t *e)
{
    int delta = (int)(intptr_t)lv_event_get_user_data(e);
    int v = (int)s_brightness + delta;
    if (v < BRIGHTNESS_MIN || v > BRIGHTNESS_MAX) return;
    s_brightness = (uint8_t)v;
    set_brightness_text();
    if (s_cb.on_brightness) s_cb.on_brightness(s_brightness);
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

static void on_preset_name_long(lv_event_t *e);
static void show_view(nano_view_t view, bool notify);

static void build_main(lv_obj_t *scr)
{
    s_main = ui_box(scr, 0, 0, SCREEN_W, SCREEN_H, C_BG);
    lv_obj_add_event_cb(s_main, ui_on_pressed, LV_EVENT_PRESSED, NULL);

    /* Top bar: status dot + text, tempo, gate, menu button. */
    s_status_dot = ui_dot(s_main, 8, 8, 8);
    s_status = ui_label(s_main, &lv_font_montserrat_12, C_MUTED);
    lv_obj_set_pos(s_status, 22, 5);
    lv_obj_set_size(s_status, 112, lv_font_get_line_height(&lv_font_montserrat_12)); /* one line: LONG_DOT needs a fixed height */
    lv_label_set_long_mode(s_status, LV_LABEL_LONG_DOT);
    /* Outputs 1/2 muted (Cortex Cloud's global switch): a red badge you cannot miss on stage. */
    s_mute_badge = ui_box(s_main, 138, 3, 50, 16, C_ERROR);
    lv_obj_set_style_radius(s_mute_badge, 4, 0);
    lv_obj_t *mute_l = ui_label(s_mute_badge, &montserrat_medium_10, C_FX_TEXT);
    lv_label_set_text(mute_l, "MUTED");
    lv_obj_center(mute_l);
    lv_obj_set_hidden(s_mute_badge, true);
    s_tempo = ui_label(s_main, &lv_font_montserrat_12, C_ON);
    lv_obj_set_pos(s_tempo, 190, 5);
    lv_obj_set_width(s_tempo, 80);
    lv_obj_set_style_text_align(s_tempo, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_t *menu = ui_button(s_main, SCREEN_W - 44, 0, 44, TOP_H + 4, LV_SYMBOL_LIST, &lv_font_montserrat_14, C_BG, C_MUTED, on_menu, NULL);
    lv_obj_add_event_cb(menu, ui_on_pressed, LV_EVENT_PRESSED, NULL);
    lv_obj_set_ext_click_area(menu, 6);

    /* Preset row: prev | label + name | next. */
    const int32_t nav_y = ROW_Y + (ROW_H - NAV_H) / 2;
    s_prev = ui_button(s_main, EDGE_X, nav_y, NAV_W, NAV_H, LV_SYMBOL_LEFT, &lv_font_montserrat_20, C_PANEL, C_MUTED, on_prev, NULL);
    s_next = ui_button(s_main, RIGHT_X - NAV_W, nav_y, NAV_W, NAV_H, LV_SYMBOL_RIGHT, &lv_font_montserrat_20, C_PANEL, C_MUTED, on_next, NULL);
    lv_obj_add_event_cb(s_prev, ui_on_pressed, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(s_next, ui_on_pressed, LV_EVENT_PRESSED, NULL);
    s_preset_label = ui_label(s_main, &lv_font_montserrat_24, C_TEXT);
    lv_obj_set_pos(s_preset_label, NAV_W + 6, ROW_Y + 4);
    s_preset_name = ui_label(s_main, &lv_font_montserrat_32, C_TEXT);
    lv_label_set_long_mode(s_preset_name, LV_LABEL_LONG_WRAP);
    lv_label_set_text(s_preset_name, "NanoGig");
    /* Long press on the label or the name: rename (a tap does nothing, so no keyboard by accident). */
    lv_obj_t *name_parts[2] = { s_preset_label, s_preset_name };
    for (int i = 0; i < 2; i++) {
        lv_obj_set_clickable(name_parts[i], true);
        lv_obj_add_event_cb(name_parts[i], ui_on_pressed, LV_EVENT_PRESSED, NULL);
        lv_obj_add_event_cb(name_parts[i], on_preset_name_long, LV_EVENT_LONG_PRESSED, NULL);
    }
    for (int i = 0; i < 4; i++) {
        lv_obj_t *b = ui_box(s_main, 0, 0, BADGE_W, BADGE_H, FS_BG[i]);
        lv_obj_set_style_radius(b, 4, 0);
        lv_obj_t *l = ui_label(b, &montserrat_medium_10, FS_FG[i]);
        lv_label_set_text(l, FS_NAMES[i]);
        lv_obj_center(l);
        lv_obj_set_hidden(b, true);
        s_fs_badges[i] = b;
    }

    /* Gate button, then the capture / IR lines. */
    s_gate = ui_button(s_main, EDGE_X, LINES_Y + (LINES_H - GATE_H) / 2, GATE_W, GATE_H, "GATE", &montserrat_medium_10, C_OFF, C_TEXT, on_gate_clicked, NULL);
    lv_obj_set_style_radius(s_gate, 7, 0);
    lv_obj_add_event_cb(s_gate, ui_on_pressed, LV_EVENT_PRESSED, NULL);
    lv_obj_set_ext_click_area(s_gate, 8);
    /* Presets list button, mirroring the gate at the right edge. */
    s_list_btn = ui_button(s_main, RIGHT_X - GATE_W, LINES_Y + (LINES_H - GATE_H) / 2, GATE_W, GATE_H, "LIST", &montserrat_medium_10, C_OFF, C_TEXT, on_open_presets, NULL);
    lv_obj_set_style_radius(s_list_btn, 7, 0);
    lv_obj_add_event_cb(s_list_btn, ui_on_pressed, LV_EVENT_PRESSED, NULL);
    lv_obj_set_ext_click_area(s_list_btn, 8);
    const int32_t lx = EDGE_X + GATE_W + 10;
    const int32_t lw = RIGHT_X - GATE_W - 8 - (lx + 15);
    s_capture_dot = ui_dot(s_main, lx, LINES_Y + 6, 9);
    s_capture = ui_label(s_main, &lv_font_montserrat_12, C_TEXT);
    lv_obj_set_pos(s_capture, lx + 15, LINES_Y + 2);
    lv_obj_set_size(s_capture, lw, lv_font_get_line_height(&lv_font_montserrat_12)); /* one line: LONG_DOT needs a fixed height */
    lv_label_set_long_mode(s_capture, LV_LABEL_LONG_DOT);
    /* Hold the capture line (dot or name) for the capture page, like the preset name for rename. */
    lv_obj_t *cap_hit = ui_box(s_main, lx - 4, LINES_Y, 15 + lw + 8, 20, C_BG);
    lv_obj_set_style_bg_opa(cap_hit, LV_OPA_TRANSP, 0);
    lv_obj_set_clickable(cap_hit, true);
    lv_obj_add_event_cb(cap_hit, ui_on_pressed, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(cap_hit, on_open_capture, LV_EVENT_LONG_PRESSED, NULL);
    /* Hold the IR line below it for the IR tab of the same page. */
    lv_obj_t *ir_hit = ui_box(s_main, lx - 4, LINES_Y + 20, 15 + lw + 8, 20, C_BG);
    lv_obj_set_style_bg_opa(ir_hit, LV_OPA_TRANSP, 0);
    lv_obj_set_clickable(ir_hit, true);
    lv_obj_add_event_cb(ir_hit, ui_on_pressed, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(ir_hit, on_open_ir, LV_EVENT_LONG_PRESSED, NULL);
    s_ir_dot = ui_dot(s_main, lx, LINES_Y + 26, 9);
    s_ir = ui_label(s_main, &lv_font_montserrat_12, C_MUTED);
    lv_obj_set_pos(s_ir, lx + 15, LINES_Y + 22);
    lv_obj_set_size(s_ir, lw, lv_font_get_line_height(&lv_font_montserrat_12)); /* one line: LONG_DOT needs a fixed height */
    lv_label_set_long_mode(s_ir, LV_LABEL_LONG_DOT);

    /* Five FX tiles: pre1 pre2 | post1 post2 post3. */
    const int32_t gap = 3, group_gap = 2; /* 5 x 60 + 4 x 3 + 2 = 314: from EDGE_X to RIGHT_X */
    for (int i = 0; i < NANO_FX_SLOT_COUNT; i++) {
        lv_obj_t *t = ui_box(s_main, EDGE_X + i * (TILE_W + gap) + (i >= 2 ? group_gap : 0), TILE_Y, TILE_W, TILE_H, C_PANEL);
        lv_obj_set_style_radius(t, 10, 0);
        lv_obj_set_style_border_width(t, 3, 0);
        lv_obj_set_style_border_side(t, LV_BORDER_SIDE_TOP, 0);
        lv_obj_set_style_border_color(t, lv_color_hex(C_DIM), 0);
        lv_obj_set_style_pad_all(t, 2, 0);
        lv_obj_set_clickable(t, true);
        lv_obj_set_ext_click_area(t, 6);
        lv_obj_add_event_cb(t, ui_on_pressed, LV_EVENT_PRESSED, NULL);
        lv_obj_add_event_cb(t, on_tile_clicked, LV_EVENT_CLICKED, (void *)(uintptr_t)i);
        lv_obj_t *name = ui_label(t, &montserrat_medium_12, C_DIM);
        lv_obj_set_width(name, TILE_W - 4);
        lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_long_mode(name, LV_LABEL_LONG_WRAP);
        lv_obj_align(name, LV_ALIGN_CENTER, 0, TILE_NAME_DY);
        lv_label_set_text(name, i < 2 ? "PRE" : "POST");
        /* Category tag ("CMP", "DLY", ...) under the top border; it never moves, only recolours. */
        lv_obj_t *tag = ui_label(t, &montserrat_bold_10, C_DIM);
        lv_obj_set_width(tag, TILE_W - 4);
        lv_obj_set_style_text_align(tag, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(tag, 0, TILE_TAG_Y);
        lv_label_set_text(tag, "");
        s_tiles[i] = t;
        s_tile_names[i] = name;
        s_tile_tags[i] = tag;
        /* Expression track along the bottom edge (inside the 2 px padding): black track, translucent
         * band for the assigned range, solid fill for the value. Hidden until the pedal moves. */
        const int32_t tw = TILE_W - 2 * 2 - 2 * TILE_EXP_INSET;
        lv_obj_t *track = ui_box(t, TILE_EXP_INSET, TILE_H - 2 * 2 - TILE_EXP_H - 3, tw, TILE_EXP_H, 0x000000);
        lv_obj_set_style_bg_opa(track, LV_OPA_70, 0);
        lv_obj_set_style_radius(track, TILE_EXP_H / 2, 0);
        lv_obj_t *band = ui_box(track, 0, 0, tw, TILE_EXP_H, C_TEXT);
        lv_obj_set_style_bg_opa(band, LV_OPA_40, 0);
        lv_obj_set_style_radius(band, TILE_EXP_H / 2, 0);
        lv_obj_t *fill = ui_box(track, 0, 1, 0, TILE_EXP_H - 2, C_TEXT);
        lv_obj_set_style_radius(fill, (TILE_EXP_H - 2) / 2, 0);
        lv_obj_set_hidden(track, true);
        s_tile_exp[i] = track;
        s_tile_exp_band[i] = band;
        s_tile_exp_fill[i] = fill;
    }

    /* Expression pedal position: a thin orange bar in the right gutter, filling from the heel (bottom). */
    s_exp_bar = ui_box(s_main, EXP_BAR_X, EXP_BAR_Y, EXP_BAR_W, EXP_BAR_H, C_PANEL_2);
    lv_obj_set_style_radius(s_exp_bar, 1, 0);
    s_exp_fill = ui_box(s_main, EXP_BAR_X, EXP_BAR_Y + EXP_BAR_H, EXP_BAR_W, 0, C_WARN);
    lv_obj_set_style_radius(s_exp_fill, 1, 0);
    lv_obj_set_hidden(s_exp_bar, true);
    lv_obj_set_hidden(s_exp_fill, true);
}

/* Redraw the expression indicators from the position, the assignments and the last values. */
static void refresh_expression(void)
{
    bool live = s_exp_show;
    int pos = s_exp_pos < 0 ? 0 : s_exp_pos;
    lv_obj_set_hidden(s_exp_bar, !live);
    lv_obj_set_hidden(s_exp_fill, !live);
    if (live) {
        int32_t h = (EXP_BAR_H * pos + 127) / 254;
        lv_obj_set_size(s_exp_fill, EXP_BAR_W, h);
        lv_obj_set_pos(s_exp_fill, EXP_BAR_X, EXP_BAR_Y + EXP_BAR_H - h);
    }
    const int32_t tw = TILE_W - 2 * 2 - 2 * TILE_EXP_INSET;
    for (int i = 0; i < NANO_FX_SLOT_COUNT; i++) {
        const nano_exp_range_t *r = s_exp_assign_valid ? &s_exp_assign.fx_range[i] : NULL;
        bool has_range = r && r->assigned;
        bool has_bypass = s_exp_assign_valid && s_exp_assign.fx_bypass_mode[i] != 0;
        bool show = live && s_tile_present[i] && (has_range || has_bypass);
        lv_obj_set_hidden(s_tile_exp[i], !show);
        if (!show) continue;
        lv_obj_set_style_bg_color(s_tile_exp_band[i], lv_color_hex(s_tile_color[i]), 0);
        lv_obj_set_style_bg_color(s_tile_exp_fill[i], lv_color_hex(s_tile_color[i]), 0);
        int32_t fill_w;
        if (has_range) {
            /* Band = the range set in Cortex Cloud; fill = the value, which already lives inside it. Until
             * the first values event of this preset, derive it from the position as the pedal maps it. */
            int32_t bx = tw * r->min / 255, bw = tw * (r->max - r->min) / 255;
            if (bw < 2) bw = 2;
            lv_obj_set_pos(s_tile_exp_band[i], bx, 0);
            lv_obj_set_width(s_tile_exp_band[i], bw);
            int v = s_exp_values_valid && s_exp_values.fx_value[i] >= 0 ? s_exp_values.fx_value[i] : r->min + (r->max - r->min) * pos / 254;
            fill_w = tw * v / 255;
        } else {
            /* Bypass only: the whole track is the band; full when the switch is engaged (heel side of mid-travel). */
            lv_obj_set_pos(s_tile_exp_band[i], 0, 0);
            lv_obj_set_width(s_tile_exp_band[i], tw);
            bool engaged = s_exp_values_valid && s_exp_values.fx_bypass[i] >= 0 ? s_exp_values.fx_bypass[i] != 0 : pos < 127;
            fill_w = engaged ? tw : 0;
        }
        if (fill_w > tw) fill_w = tw;
        lv_obj_set_width(s_tile_exp_fill[i], fill_w);
        lv_obj_set_pos(s_tile_exp_fill[i], 0, 1);
    }
}

void nano_ui_set_expression(int position)
{
    s_exp_pos = position < 0 ? -1 : position > 254 ? 254 : position;
    refresh_expression();
}

void nano_ui_set_expression_show(bool show)
{
    s_exp_show = show;
    if (s_exp_toggle) refresh_exp_toggle();
    refresh_expression();
}

void nano_ui_set_expression_assignments(const nano_exp_assignments_t *a)
{
    s_exp_assign_valid = a != NULL;
    if (a) s_exp_assign = *a;
    refresh_expression();
}

void nano_ui_set_expression_values(const nano_exp_values_t *v)
{
    s_exp_values_valid = v != NULL;
    if (v) s_exp_values = *v;
    refresh_expression();
}

/* ---- menu / settings / tuner views --------------------------------------- */

/* Full-screen view with a title bar: "<" back to the menu (when `back`), the title, "x" to the gig view. */
static lv_obj_t *make_overlay_cb(lv_obj_t *scr, const char *title, lv_event_cb_t back_cb, lv_event_cb_t close_cb, lv_obj_t **close_out);
static lv_obj_t *make_overlay(lv_obj_t *scr, const char *title, bool back)
{
    return make_overlay_cb(scr, title, back ? on_back_to_menu : NULL, on_close, NULL);
}

/* Same with the callbacks given: NULL back_cb = no back button. */
static lv_obj_t *make_overlay_cb(lv_obj_t *scr, const char *title, lv_event_cb_t back_cb, lv_event_cb_t close_cb, lv_obj_t **close_out)
{
    bool back = back_cb != NULL;
    lv_obj_t *o = ui_box(scr, 0, 0, SCREEN_W, SCREEN_H, C_BG);
    lv_obj_add_event_cb(o, ui_on_pressed, LV_EVENT_PRESSED, NULL);
    if (back) {
        lv_obj_t *b = ui_button(o, 0, 0, 44, TOP_H + 4, LV_SYMBOL_LEFT, &lv_font_montserrat_14, C_BG, C_MUTED, back_cb, NULL);
        lv_obj_add_event_cb(b, ui_on_pressed, LV_EVENT_PRESSED, NULL);
        lv_obj_set_ext_click_area(b, 6);
    }
    lv_obj_t *t = ui_label(o, &lv_font_montserrat_14, C_MUTED);
    lv_label_set_text(t, title);
    lv_obj_set_pos(t, back ? 40 : 12, 8);
    lv_obj_t *close = ui_button(o, SCREEN_W - 44, 0, 44, TOP_H + 4, LV_SYMBOL_CLOSE, &lv_font_montserrat_14, C_BG, C_MUTED, close_cb, NULL);
    lv_obj_set_ext_click_area(close, 6);
    if (close_out) *close_out = close;
    lv_obj_set_hidden(o, true);
    return o;
}

static lv_obj_t *menu_item(lv_obj_t *parent, int row, const char *text, uint32_t color, lv_event_cb_t cb, lv_obj_t **label_out)
{
    const int32_t h = 44, gap = 6, y0 = 30;
    lv_obj_t *b = ui_button(parent, 12, y0 + row * (h + gap), SCREEN_W - 24, h, text, &lv_font_montserrat_20, C_PANEL, color, cb, NULL);
    lv_obj_add_event_cb(b, ui_on_pressed, LV_EVENT_PRESSED, NULL);
    if (label_out) *label_out = lv_obj_get_child(b, 0);
    return b;
}

static void build_menu(lv_obj_t *scr)
{
    s_menu = make_overlay(scr, "NanoGig Screen", false);
    /* One colour per item so a row is found without reading it. */
    menu_item(s_menu, 0, LV_SYMBOL_BLUETOOTH "  Disconnect", C_ERROR, on_link_toggle, &s_link_btn_label);
    menu_item(s_menu, 1, LV_SYMBOL_SETTINGS "  Settings", C_WARN, on_open_settings, NULL);
    menu_item(s_menu, 2, LV_SYMBOL_LOOP "  Tempo", C_ON, on_open_tempo, NULL);
    menu_item(s_menu, 3, LV_SYMBOL_AUDIO "  Tuner", 0xBFE3FF, on_open_tuner, NULL);
}

/* One settings row: caption at the left, the control right-aligned to SETTING_RIGHT (page coordinates). */

static lv_obj_t *setting_caption(lv_obj_t *parent, int32_t y, const char *text)
{
    lv_obj_t *l = ui_label(parent, &lv_font_montserrat_14, C_TEXT);
    lv_label_set_text(l, text);
    lv_obj_set_pos(l, 0, y + (SETTING_ROW_H - lv_font_get_line_height(&lv_font_montserrat_14)) / 2);
    return l;
}

/* [-] value [+] against the right edge of a settings row; returns the value label (font 20, centred). */
static lv_obj_t *setting_stepper(lv_obj_t *page, int32_t y, lv_event_cb_t cb)
{
    lv_obj_t *plus = ui_button(page, SETTING_RIGHT - 44, y, 44, SETTING_ROW_H, LV_SYMBOL_PLUS, &lv_font_montserrat_14, C_PANEL, C_TEXT, cb, (void *)(intptr_t)1);
    lv_obj_add_event_cb(plus, ui_on_pressed, LV_EVENT_PRESSED, NULL);
    lv_obj_t *value = ui_label(page, &lv_font_montserrat_20, C_TEXT);
    lv_obj_set_pos(value, SETTING_RIGHT - 44 - 40, y + (SETTING_ROW_H - lv_font_get_line_height(&lv_font_montserrat_20)) / 2);
    lv_obj_set_width(value, 40);
    lv_obj_set_style_text_align(value, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_t *minus = ui_button(page, SETTING_RIGHT - 44 - 40 - 44, y, 44, SETTING_ROW_H, LV_SYMBOL_MINUS, &lv_font_montserrat_14, C_PANEL, C_TEXT, cb, (void *)(intptr_t)-1);
    lv_obj_add_event_cb(minus, ui_on_pressed, LV_EVENT_PRESSED, NULL);
    return value;
}

static void build_settings(lv_obj_t *scr)
{
    s_settings = make_overlay(scr, "Settings", true);
    /* Pager under the title bar (its column only shows once there is more than one page). */
    const int32_t top = TOP_H + 6, page_h = SCREEN_H - top - 6;
    pager_create(&s_settings_pager, s_settings, top, page_h, "Page");
    lv_obj_t *page1 = pager_add_page(&s_settings_pager, s_settings, SETTING_X, top, SCREEN_W - SETTING_X, page_h);

    /* Page 1. Presets per bank:  [-] 4 [+] */
    int32_t y = 4;
    setting_caption(page1, y, "Presets per bank");
    s_bank_value = setting_stepper(page1, y, on_bank_step);
    set_bank_value_text();

    /* Preset label style: [1B] [A2] [1..64] with a one-line meaning under it. */
    y = 52;
    setting_caption(page1, y, "Preset label");
    static const char *const SEG[3] = { "1B", "A2", "1..64" };
    const int32_t seg_w = 54, seg_gap = 4;
    for (int i = 0; i < 3; i++) {
        int32_t x = SETTING_RIGHT - (3 - i) * seg_w - (2 - i) * seg_gap;
        s_style_seg[i] = ui_button(page1, x, y, seg_w, SETTING_ROW_H, SEG[i], &lv_font_montserrat_14, C_PANEL, C_TEXT, on_style_clicked, (void *)(uintptr_t)i);
        lv_obj_add_event_cb(s_style_seg[i], ui_on_pressed, LV_EVENT_PRESSED, NULL);
        lv_obj_set_style_radius(s_style_seg[i], 8, 0);
    }
    s_style_hint = ui_label(page1, &lv_font_montserrat_12, C_MUTED);
    lv_obj_set_pos(s_style_hint, SETTING_RIGHT - 3 * seg_w - 2 * seg_gap, y + SETTING_ROW_H + 3);
    lv_obj_set_width(s_style_hint, 3 * seg_w + 2 * seg_gap);
    lv_obj_set_style_text_align(s_style_hint, LV_TEXT_ALIGN_CENTER, 0);
    refresh_style_seg();

    /* Mute outputs 1/2: a switch, red while muted (it silences the whole rig). */
    y = 118;
    setting_caption(page1, y, "Mute outputs 1/2");
    s_mute_toggle = make_toggle(page1, y, on_mute_toggle_clicked, &s_mute_knob);
    refresh_mute();

    /* Expression pedal indicators: the side bar and the tile tracks (~40 redraws/s while the pedal moves). */
    y = 158;
    setting_caption(page1, y, "Show expression pedal");
    s_exp_toggle = make_toggle(page1, y, on_exp_toggle_clicked, &s_exp_knob);
    refresh_exp_toggle();

    /* Page 2: the display itself. */
    lv_obj_t *page2 = pager_add_page(&s_settings_pager, s_settings, SETTING_X, top, SCREEN_W - SETTING_X, page_h);

    /* Rotate display: [0°] [180°] (the USB lead leaves left or right, depending on the mount). */
    y = 4;
    setting_caption(page2, y, "Rotate display");
    static const char *const ROT[2] = { "0°", "180°" };
    const int32_t rot_w = 70, rot_gap = 4;
    for (int i = 0; i < 2; i++) {
        int32_t x = SETTING_RIGHT - (2 - i) * rot_w - (1 - i) * rot_gap;
        s_rot_seg[i] = ui_button(page2, x, y, rot_w, SETTING_ROW_H, ROT[i], &lv_font_montserrat_14, C_PANEL, C_TEXT, on_rot_clicked, (void *)(uintptr_t)i);
        lv_obj_add_event_cb(s_rot_seg[i], ui_on_pressed, LV_EVENT_PRESSED, NULL);
        lv_obj_set_style_radius(s_rot_seg[i], 8, 0);
    }
    refresh_rot_seg();

    /* Brightness: [-] 10 [+], ten steps, applied as you tap. */
    y = 52;
    setting_caption(page2, y, "Brightness");
    s_bright_value = setting_stepper(page2, y, on_brightness_step);
    set_brightness_text();

    /* Page 3: firmware version and the way into the update view. */
    lv_obj_t *page3 = pager_add_page(&s_settings_pager, s_settings, SETTING_X, top, SCREEN_W - SETTING_X, page_h);
    y = 4;
    setting_caption(page3, y, "Firmware");
    s_fw_value = ui_label(page3, &lv_font_montserrat_14, C_MUTED);
    lv_obj_set_width(s_fw_value, 150);
    lv_obj_set_style_text_align(s_fw_value, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_long_mode(s_fw_value, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(s_fw_value, SETTING_RIGHT - 150, y + (SETTING_ROW_H - lv_font_get_line_height(&lv_font_montserrat_14)) / 2);
    lv_label_set_text(s_fw_value, s_fw_version);
    y = 52;
    lv_obj_t *upd = ui_button(page3, 0, y, SETTING_RIGHT, 40, LV_SYMBOL_DOWNLOAD "  Check for updates", &lv_font_montserrat_14, C_PANEL, C_ACCENT, on_open_update, NULL);
    lv_obj_add_event_cb(upd, ui_on_pressed, LV_EVENT_PRESSED, NULL);
    lv_obj_t *hint = ui_label(page3, &lv_font_montserrat_12, C_MUTED);
    lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(hint, SETTING_RIGHT);
    lv_obj_set_pos(hint, 0, y + 48);
    lv_label_set_text(hint, "Joins Wi-Fi and turns Bluetooth off while it runs. The screen restarts when you close the update page.");

    pager_show(&s_settings_pager, 0);
}

/* Presets list: one bank per page (Settings > Presets per bank), the pager steps through the banks. */
#define PRESET_ROWS_MAX 8
#define PRESET_ROW_GAP 4
#define PRESET_ROW_H_MAX 48

static pager_t s_presets_pager;
static lv_obj_t *s_preset_rows[PRESET_ROWS_MAX], *s_preset_row_tag[PRESET_ROWS_MAX], *s_preset_row_name[PRESET_ROWS_MAX];
static int32_t s_presets_top, s_presets_h;
static int s_presets_return_bank = -1; /* reopen on this bank (back from renaming one of its presets) */

static void on_preset_row_clicked(lv_event_t *e)
{
    int row = (int)(uintptr_t)lv_event_get_user_data(e);
    int idx = s_presets_pager.current * s_per_bank + row;
    if (idx >= NANO_PRESET_COUNT) return;
    if (s_cb.on_select_preset) s_cb.on_select_preset((uint8_t)idx);
    nano_ui_show(s_base_view);
}

/* Long press on a row: rename that preset ("<" comes back to this list). */
static void on_preset_row_long(lv_event_t *e)
{
    int row = (int)(uintptr_t)lv_event_get_user_data(e);
    int idx = s_presets_pager.current * s_per_bank + row;
    if (idx >= NANO_PRESET_COUNT) return;
    s_presets_return_bank = s_presets_pager.current;
    nano_ui_open_rename((uint8_t)idx);
}

/* Fill the rows for one bank: as many as presets per bank, sized to share the page height. */
static void presets_show_bank(int bank)
{
    int n = s_per_bank;
    int32_t h = (s_presets_h - (n - 1) * PRESET_ROW_GAP) / n;
    if (h > PRESET_ROW_H_MAX) h = PRESET_ROW_H_MAX;
    const lv_font_t *font = h >= 40 ? &lv_font_montserrat_20 : &lv_font_montserrat_14;
    /* Names line up after the widest label of the bank ("4C", "22A", "64"). */
    int32_t tag_w = 0;
    for (int i = 0; i < n && bank * n + i < NANO_PRESET_COUNT; i++) {
        char label[8];
        nano_preset_label((uint8_t)(bank * n + i), s_per_bank, s_label_style, label, sizeof(label));
        lv_point_t size;
        lv_text_get_size(&size, label, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        if (size.x > tag_w) tag_w = size.x;
    }
    tag_w += 10;
    for (int i = 0; i < PRESET_ROWS_MAX; i++) {
        int idx = bank * n + i;
        lv_obj_t *row = s_preset_rows[i];
        bool shown = i < n && idx < NANO_PRESET_COUNT;
        lv_obj_set_hidden(row, !shown);
        if (!shown) continue;
        lv_obj_set_pos(row, SETTING_X, s_presets_top + i * (h + PRESET_ROW_GAP));
        lv_obj_set_height(row, h);
        /* The shown preset: a green outline. */
        lv_obj_set_style_border_width(row, idx == s_preset ? 2 : 0, 0);

        char label[8];
        nano_preset_label((uint8_t)idx, s_per_bank, s_label_style, label, sizeof(label));
        lv_obj_t *tag = s_preset_row_tag[i];
        lv_label_set_text(tag, label);
        lv_obj_set_style_text_font(tag, font, 0);
        lv_obj_set_style_text_color(tag, lv_color_hex(SLOT_COLORS[i & 7]), 0);
        lv_obj_align(tag, LV_ALIGN_LEFT_MID, 8, 0);

        const char *name = s_meta ? s_meta->presets[idx].name : "";
        char fallback[24];
        if (!name[0]) {
            snprintf(fallback, sizeof(fallback), "Preset %u", (unsigned)idx + 1);
            name = fallback;
        }
        lv_obj_t *nl = s_preset_row_name[i];
        lv_obj_set_style_text_font(nl, font, 0);
        lv_obj_set_size(nl, SETTING_RIGHT - 8 - tag_w - 8, lv_font_get_line_height(font));
        lv_label_set_text(nl, name);
        lv_obj_align(nl, LV_ALIGN_LEFT_MID, 8 + tag_w, 0);
    }
}

static void build_presets(lv_obj_t *scr)
{
    s_presets = make_overlay_cb(scr, "Presets", on_close, on_close, NULL);
    s_presets_top = TOP_H + 6;
    s_presets_h = SCREEN_H - s_presets_top - 6;
    pager_create(&s_presets_pager, s_presets, s_presets_top, s_presets_h, "Bank");
    s_presets_pager.on_show = presets_show_bank;
    s_presets_pager.wrap = true; /* like prev / next on the gig view: bank 16 -> bank 1 */
    for (int i = 0; i < PRESET_ROWS_MAX; i++) {
        lv_obj_t *row = ui_box(s_presets, SETTING_X, s_presets_top, SETTING_RIGHT, PRESET_ROW_H_MAX, C_PANEL);
        lv_obj_set_style_radius(row, 8, 0);
        lv_obj_set_style_border_color(row, lv_color_hex(C_ON), 0);
        lv_obj_set_clickable(row, true);
        lv_obj_set_style_bg_color(row, lv_color_hex(C_TEXT), LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(row, LV_OPA_30, LV_STATE_PRESSED);
        lv_obj_add_event_cb(row, ui_on_pressed, LV_EVENT_PRESSED, NULL);
        lv_obj_add_event_cb(row, on_preset_row_clicked, LV_EVENT_SHORT_CLICKED, (void *)(uintptr_t)i);
        lv_obj_add_event_cb(row, on_preset_row_long, LV_EVENT_LONG_PRESSED, (void *)(uintptr_t)i);
        s_preset_row_tag[i] = ui_label(row, &lv_font_montserrat_14, C_TEXT);
        s_preset_row_name[i] = ui_label(row, &lv_font_montserrat_14, C_TEXT);
        lv_label_set_long_mode(s_preset_row_name[i], LV_LABEL_LONG_DOT);
        lv_obj_set_hidden(row, true);
        s_preset_rows[i] = row;
    }
}

/* Open on the bank of the shown preset, or the one a rename started from. */
static void presets_open(void)
{
    int banks = (NANO_PRESET_COUNT + s_per_bank - 1) / s_per_bank;
    int bank = s_presets_return_bank >= 0 && s_presets_return_bank < banks ? s_presets_return_bank : s_preset / s_per_bank;
    s_presets_return_bank = -1;
    pager_set_count(&s_presets_pager, banks, bank);
}

/*
 * Capture page: name and on / off dot, then the volume as a ui_value_ctrl in Cortex Cloud's dB scale
 * and readout (-24..+12 dB, tenths cut toward zero; nano_capture_volume_*): the slider runs in
 * tenths of a dB, steps of 0.1 and 1 dB, double tap on the value for 0.0 dB (raw 128).
 */
static int cap_vol_pos(int raw) { return (int)lroundf(nano_capture_volume_db((uint8_t)raw) * 10.0f); }
static int cap_vol_raw(int pos) { return nano_capture_volume_raw(pos / 10.0f); }
static int cap_vol_readout(int raw) { return nano_capture_volume_tenths((uint8_t)raw); }

static void cap_vol_format(int raw, char *out, size_t cap)
{
    int t = nano_capture_volume_tenths((uint8_t)raw);
    snprintf(out, cap, "%c%d.%d dB", t < 0 ? '-' : '+', abs(t) / 10, abs(t) % 10);
}

static void cap_vol_changed(int raw, void *user)
{
    (void)user;
    s_cap_volume = raw;
    if (s_cb.on_capture_volume) s_cb.on_capture_volume((uint8_t)raw);
}

static void capture_refresh(void)
{
    if (!s_capture_view || s_tab_built != 0) return;
    lv_obj_set_style_bg_color(s_cap_dot, lv_color_hex(s_cap_on ? C_ON : C_DIM), 0);
    lv_label_set_text(s_cap_name_l, s_cap_name[0] ? s_cap_name : "No capture");
    lv_obj_set_style_text_color(s_cap_name_l, lv_color_hex(s_cap_on ? C_TEXT : C_OFF_TEXT), 0);
}

/* A state dump's capture. While the page is open the control decides whether the volume shows
 * (it ignores reports that were requested before its latest change). */
static void capture_from_state(const nano_state_t *st)
{
    snprintf(s_cap_name, sizeof(s_cap_name), "%s", st->capture_name);
    s_cap_on = st->capture_on;
    s_cap_volume = st->capture_volume;
    s_cap_preset = st->active_preset;
    if (s_cap_vol) {
        ui_value_ctrl_report(s_cap_vol, s_cap_volume, s_cap_preset);
        s_cap_volume = ui_value_ctrl_value(s_cap_vol);
    }
    capture_refresh();
}

/* Capture / IR: one page with a tab per source in the header (a view each, NANO_VIEW_CAPTURE / _IR). */
#define TAB_TOP (TOP_H + 4)

static void on_tab(lv_event_t *e)
{
    nano_ui_show(lv_event_get_target_obj(e) == s_tab_btn[1] ? NANO_VIEW_IR : NANO_VIEW_CAPTURE);
}

static void build_capture_tab(lv_obj_t *page);
static void build_ir_tab(lv_obj_t *page);

/* Only the tab showing exists: both at once ran the heap out (2026-10-09, a crash drawing the pager label). The
 * tap comes from the header, so the content can go at once. */
static void tabs_select(int tab)
{
    for (int i = 0; i < 2; i++) {
        bool on = i == tab;
        lv_obj_set_style_bg_opa(s_tab_btn[i], on ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(s_tab_btn[i], on ? 2 : 0, 0);
        lv_obj_set_style_text_color(lv_obj_get_child(s_tab_btn[i], 0), lv_color_hex(on ? C_TEXT : C_MUTED), 0);
    }
    if (s_tab_built == tab) return;
    lv_obj_clean(s_tab_page);
    s_cap_vol = NULL; /* freed with the content */
    memset(s_ir_ctrl, 0, sizeof(s_ir_ctrl));
    memset(s_ir_mic_btn, 0, sizeof(s_ir_mic_btn));
    memset(s_ir_pos_btn, 0, sizeof(s_ir_pos_btn));
    s_ir_mic_note = NULL;
    s_tab_built = tab;
    if (tab == 0) build_capture_tab(s_tab_page);
    else build_ir_tab(s_tab_page);
}

static void build_capture_tab(lv_obj_t *page)
{
    /* Name with its on / off dot (up to two lines). */
    s_cap_dot = ui_dot(page, 14, 44 - TAB_TOP, 12);
    s_cap_name_l = ui_label(page, &lv_font_montserrat_20, C_TEXT);
    lv_obj_set_pos(s_cap_name_l, 34, 38 - TAB_TOP);
    lv_obj_set_width(s_cap_name_l, SCREEN_W - 34 - 12);
    lv_label_set_long_mode(s_cap_name_l, LV_LABEL_LONG_WRAP);

    static const ui_value_ctrl_cfg_t vol = {
        .raw_min = 0, .raw_max = 255,
        .pos_min = -240, .pos_max = 120,
        .raw_to_pos = cap_vol_pos, .pos_to_raw = cap_vol_raw,
        .readout = cap_vol_readout, .format = cap_vol_format,
        .fine = 1, .coarse = 10,
        .step_labels = { "-1 dB", "-0.1", "+0.1", "+1 dB" },
        .reset_raw = 128,
        .on_change = cap_vol_changed,
    };
    ui_value_ctrl_cfg_t cfg = vol;
    if (!s_cb.on_capture_volume) cfg.on_change = NULL; /* read-only */
    s_cap_vol = ui_value_ctrl_create(page, 92 - TAB_TOP, &cfg);
    ui_value_ctrl_report(s_cap_vol, s_cap_volume, s_cap_preset);

    if (!s_cb.on_capture_volume) {
        lv_obj_t *hint = ui_label(page, &lv_font_montserrat_12, C_MUTED);
        lv_obj_set_width(hint, SCREEN_W);
        lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(hint, 0, 92 - TAB_TOP + UI_VALUE_CTRL_HEIGHT + 2);
        lv_label_set_text(hint, "Read-only for now");
    }
}

/*
 * IR tab: name, on / off dot and Phase, then a page per setting of Cortex Cloud's IR loader: Level, High pass,
 * Low pass, then microphone and position (factory IRs only). The sliders run on the pedal's own 0..1, like
 * Cortex Cloud's; the steps count in readout units: Level tenths of a dB (raw = tenths + 960), High pass
 * Hz, Low pass Hz with 100 Hz steps.
 */
#define IR_LEVEL_ZERO 960 /* raw of 0.0 dB: -96.0 dB = raw 0 */

static int ir_level_pos(int raw) { return (int)lroundf(nano_cab_normalized(NANO_CAB_LEVEL, (raw - IR_LEVEL_ZERO) / 10.0f) * 1000.0f); }
static int ir_level_raw(int pos) { return (int)lroundf(nano_cab_value(NANO_CAB_LEVEL, pos / 1000.0f) * 10.0f) + IR_LEVEL_ZERO; }
static int ir_hp_pos(int raw) { return (int)lroundf(nano_cab_normalized(NANO_CAB_HIGH_PASS, (float)raw) * 1000.0f); }
static int ir_hp_raw(int pos) { return (int)lroundf(nano_cab_value(NANO_CAB_HIGH_PASS, pos / 1000.0f)); }
static int ir_lp_pos(int raw) { return (int)lroundf(nano_cab_normalized(NANO_CAB_LOW_PASS, (float)raw) * 1000.0f); }
static int ir_lp_raw(int pos) { return (int)lroundf(nano_cab_value(NANO_CAB_LOW_PASS, pos / 1000.0f)); }
static int ir_same(int v) { return v; }
static int ir_lp_readout(int raw) { return raw / 100; }

static void ir_level_format(int raw, char *out, size_t cap)
{
    int t = raw - IR_LEVEL_ZERO;
    snprintf(out, cap, "%c%d.%d dB", t < 0 ? '-' : '+', abs(t) / 10, abs(t) % 10);
}

static void ir_hz_format(int raw, char *out, size_t cap) { snprintf(out, cap, "%d Hz", raw); }

static void ir_khz_format(int raw, char *out, size_t cap)
{
    int r = raw / 100;
    snprintf(out, cap, "%d.%d kHz", r / 10, r % 10);
}

/* The pedal's 0..1 as a control's raw value, and back. */
static int ir_raw_of(nano_cab_param_t p, float n)
{
    float v = nano_cab_value(p, n);
    return p == NANO_CAB_LEVEL ? (int)lroundf(v * 10.0f) + IR_LEVEL_ZERO : (int)lroundf(v);
}

static float ir_normalized_of(nano_cab_param_t p, int raw)
{
    return nano_cab_normalized(p, p == NANO_CAB_LEVEL ? (raw - IR_LEVEL_ZERO) / 10.0f : (float)raw);
}

static void ir_changed(int raw, void *user)
{
    nano_cab_param_t p = (nano_cab_param_t)(intptr_t)user;
    s_ir_set.values[p] = ir_normalized_of(p, raw);
    if (s_cb.on_cab_setting) s_cb.on_cab_setting((uint8_t)p, s_ir_set.values[p]);
}

static const ui_value_ctrl_cfg_t IR_CFG[NANO_CAB_PARAMS] = {
    [NANO_CAB_LEVEL] = {
        .raw_min = 0, .raw_max = IR_LEVEL_ZERO + 120,
        .pos_min = 0, .pos_max = 1000,
        .raw_to_pos = ir_level_pos, .pos_to_raw = ir_level_raw,
        .readout = ir_same, .format = ir_level_format,
        .fine = 1, .coarse = 10,
        .step_labels = { "-1 dB", "-0.1", "+0.1", "+1 dB" },
        .reset_raw = IR_LEVEL_ZERO,
        .turn = 6,
    },
    [NANO_CAB_HIGH_PASS] = {
        .raw_min = 20, .raw_max = 800,
        .pos_min = 0, .pos_max = 1000,
        .raw_to_pos = ir_hp_pos, .pos_to_raw = ir_hp_raw,
        .readout = ir_same, .format = ir_hz_format,
        .fine = 1, .coarse = 10,
        .step_labels = { "-10 Hz", "-1", "+1", "+10 Hz" },
        .reset_raw = -1,
        .turn = 6,
    },
    [NANO_CAB_LOW_PASS] = {
        .raw_min = 1000, .raw_max = 20000,
        .pos_min = 0, .pos_max = 1000,
        .raw_to_pos = ir_lp_pos, .pos_to_raw = ir_lp_raw,
        .readout = ir_lp_readout, .format = ir_khz_format,
        .fine = 1, .coarse = 10,
        .step_labels = { "-1 kHz", "-0.1", "+0.1", "+1 kHz" },
        .reset_raw = -1,
        .turn = 6,
    },
};
static const char *const IR_CAPTIONS[NANO_CAB_PARAMS] = { "Level", "High pass", "Low pass" };

static pager_t s_ir_pager;

static void ir_choice_style(lv_obj_t *b, bool selected, bool enabled)
{
    lv_obj_set_style_border_width(b, selected ? 2 : 0, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(selected ? C_PANEL_2 : C_PANEL), 0);
    lv_obj_set_style_opa(b, enabled ? LV_OPA_COVER : LV_OPA_40, 0);
    if (enabled) lv_obj_remove_state(b, LV_STATE_DISABLED);
    else lv_obj_add_state(b, LV_STATE_DISABLED);
}

/* Microphone page: the IR's microphones (as the pedal lists them) and the six positions. */
static void ir_refresh_mics(void)
{
    bool usable = s_ir_on && s_ir_known && s_ir_set.factory && s_cb.on_cab_mic;
    if (!s_ir_mic_note) return; /* another page shows */
    for (int i = 0; i < IR_MIC_BUTTONS; i++) {
        lv_obj_t *b = s_ir_mic_btn[i];
        bool shown = s_ir_known && s_ir_set.factory && i < s_ir_set.mic_count;
        lv_obj_set_hidden(b, !shown);
        if (!shown) continue;
        lv_label_set_text(lv_obj_get_child(b, 0), s_ir_set.mics[i]);
        ir_choice_style(b, strcmp(s_ir_set.mics[i], s_ir_set.mic) == 0, usable);
    }
    for (int i = 0; i < NANO_CAB_POSITIONS; i++) {
        lv_obj_set_hidden(s_ir_pos_btn[i], !(s_ir_known && s_ir_set.factory));
        ir_choice_style(s_ir_pos_btn[i], s_ir_set.position == i, usable);
    }
    lv_label_set_text(s_ir_mic_note, !s_ir_known || s_ir_set.factory ? "" : "Your own IR: no microphone choice");
}

static void ir_refresh_phase(void)
{
    bool usable = s_ir_on && s_ir_known && s_cb.on_cab_phase;
    lv_obj_set_style_bg_color(s_ir_phase_btn, lv_color_hex(s_ir_set.phase_inverted ? C_ACCENT : C_PANEL), 0);
    lv_obj_set_style_text_color(lv_obj_get_child(s_ir_phase_btn, 0), lv_color_hex(s_ir_set.phase_inverted ? C_FX_TEXT : C_TEXT), 0);
    lv_obj_set_style_opa(s_ir_phase_btn, usable ? LV_OPA_COVER : LV_OPA_40, 0);
}

/* Name, hint and the controls from s_ir_*: values only while the IR is on and the pedal has answered. `fresh` =
 * the pedal's answer to a read after the last change here (shown even while a control holds its own). */
static void ir_refresh_as(bool fresh)
{
    if (!s_capture_view || s_tab_built != 1) return;
    lv_obj_set_style_bg_color(s_ir_dot_l, lv_color_hex(s_ir_on ? C_ON : C_DIM), 0);
    lv_label_set_text(s_ir_name_l, s_ir_name[0] ? s_ir_name : "No IR");
    lv_obj_set_style_text_color(s_ir_name_l, lv_color_hex(s_ir_on ? C_TEXT : C_OFF_TEXT), 0);
    lv_label_set_text(s_ir_hint, !s_ir_on ? "IR is off" : !s_ir_known ? "Reading..." : "");
    for (int i = 0; i < NANO_CAB_PARAMS; i++) {
        int raw = s_ir_on && s_ir_known ? ir_raw_of((nano_cab_param_t)i, s_ir_set.values[i]) : -1;
        if (fresh) ui_value_ctrl_set(s_ir_ctrl[i], raw, s_ir_owner);
        else ui_value_ctrl_report(s_ir_ctrl[i], raw, s_ir_owner);
    }
    ir_refresh_mics();
    ir_refresh_phase();
}

static void ir_refresh(void) { ir_refresh_as(false); }

static void on_ir_mic(lv_event_t *e)
{
    lv_obj_t *b = lv_event_get_target_obj(e);
    if (!s_ir_on || !s_ir_known || !s_ir_set.factory || !s_cb.on_cab_mic) return;
    for (int i = 0; i < IR_MIC_BUTTONS; i++) {
        if (b == s_ir_mic_btn[i] && i < s_ir_set.mic_count) snprintf(s_ir_set.mic, sizeof(s_ir_set.mic), "%s", s_ir_set.mics[i]);
    }
    for (int i = 0; i < NANO_CAB_POSITIONS; i++) {
        if (b == s_ir_pos_btn[i]) s_ir_set.position = (uint8_t)i;
    }
    ir_refresh_mics(); /* shown now; the app reads the IR again to confirm */
    s_cb.on_cab_mic(s_ir_set.position, s_ir_set.mic);
}

static void on_ir_phase(lv_event_t *e)
{
    (void)e;
    if (!s_ir_on || !s_ir_known || !s_cb.on_cab_phase) return;
    s_ir_set.phase_inverted = !s_ir_set.phase_inverted;
    ir_refresh_phase(); /* shown now; the app reads the IR again to confirm */
    s_cb.on_cab_phase(s_ir_set.phase_inverted);
}

static void build_ir_mic_page(lv_obj_t *p)
{
    const int32_t w = SCREEN_W - SETTING_X - 8, gap = 6;
    lv_obj_t *cap = ui_label(p, &lv_font_montserrat_14, C_MUTED);
    lv_label_set_text(cap, "Microphone");
    const int32_t mw = (w - 2 * gap) / 3, mh = 34;
    for (int i = 0; i < IR_MIC_BUTTONS; i++) {
        int row = i / 3, col = i % 3;
        lv_obj_t *b = ui_button(p, col * (mw + gap), 20 + row * (mh + 4), mw, mh, "", &montserrat_medium_12, C_PANEL, C_TEXT, on_ir_mic, NULL);
        lv_obj_set_style_radius(b, 8, 0);
        lv_obj_set_style_border_color(b, lv_color_hex(C_ACCENT), 0);
        lv_obj_t *l = lv_obj_get_child(b, 0);
        lv_obj_set_width(l, mw - 6);
        lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_center(l);
        lv_obj_add_event_cb(b, ui_on_pressed, LV_EVENT_PRESSED, NULL);
        lv_obj_set_hidden(b, true);
        s_ir_mic_btn[i] = b;
    }
    const int32_t py = 20 + 2 * (mh + 4) + 6;
    lv_obj_t *pc = ui_label(p, &lv_font_montserrat_14, C_MUTED);
    lv_label_set_text(pc, "Position");
    lv_obj_set_pos(pc, 0, py);
    const int32_t pw = (w - 5 * gap) / NANO_CAB_POSITIONS;
    for (int i = 0; i < NANO_CAB_POSITIONS; i++) {
        char t[4];
        snprintf(t, sizeof(t), "%d", i + 1);
        lv_obj_t *b = ui_button(p, i * (pw + gap), py + 20, pw, 34, t, &lv_font_montserrat_14, C_PANEL, C_TEXT, on_ir_mic, NULL);
        lv_obj_set_style_radius(b, 8, 0);
        lv_obj_set_style_border_color(b, lv_color_hex(C_ACCENT), 0);
        lv_obj_add_event_cb(b, ui_on_pressed, LV_EVENT_PRESSED, NULL);
        s_ir_pos_btn[i] = b;
    }
    s_ir_mic_note = ui_label(p, &lv_font_montserrat_14, C_MUTED);
    lv_obj_set_pos(s_ir_mic_note, 0, 24);
    lv_obj_set_width(s_ir_mic_note, w);
}

/* Pages 1..3 Level, High pass, Low pass; 4 microphone and position. */
static void ir_show_page(int idx)
{
    lv_obj_clean(s_ir_page);
    memset(s_ir_ctrl, 0, sizeof(s_ir_ctrl));
    memset(s_ir_mic_btn, 0, sizeof(s_ir_mic_btn));
    memset(s_ir_pos_btn, 0, sizeof(s_ir_pos_btn));
    s_ir_mic_note = NULL;
    if (idx < NANO_CAB_PARAMS) {
        lv_obj_t *cap = ui_label(s_ir_page, &lv_font_montserrat_14, C_MUTED);
        lv_label_set_text(cap, IR_CAPTIONS[idx]);
        ui_value_ctrl_cfg_t cfg = IR_CFG[idx];
        cfg.on_change = s_cb.on_cab_setting ? ir_changed : NULL;
        cfg.user = (void *)(intptr_t)idx;
        s_ir_ctrl[idx] = ui_value_ctrl_create(s_ir_page, 26, &cfg);
    } else {
        build_ir_mic_page(s_ir_page);
    }
    if (s_ir_hint) ir_refresh_as(true); /* the new page's controls start from the pedal's values */
}

static void build_ir_tab(lv_obj_t *page)
{
    /* Name with its on / off dot (one line), Phase at the right. */
    const int32_t phase_w = 58;
    s_ir_hint = NULL;
    s_ir_dot_l = ui_dot(page, 14, 44 - TAB_TOP, 12);
    s_ir_name_l = ui_label(page, &lv_font_montserrat_20, C_TEXT);
    lv_obj_set_pos(s_ir_name_l, 34, 38 - TAB_TOP);
    lv_obj_set_size(s_ir_name_l, SCREEN_W - 34 - 8 - phase_w - 8, lv_font_get_line_height(&lv_font_montserrat_20));
    lv_label_set_long_mode(s_ir_name_l, LV_LABEL_LONG_DOT);
    s_ir_phase_btn = ui_button(page, SCREEN_W - 8 - phase_w, 35 - TAB_TOP, phase_w, 28, "Phase", &lv_font_montserrat_14, C_PANEL, C_TEXT, on_ir_phase, NULL);
    lv_obj_set_style_radius(s_ir_phase_btn, 8, 0);
    lv_obj_set_ext_click_area(s_ir_phase_btn, 4);
    lv_obj_add_event_cb(s_ir_phase_btn, ui_on_pressed, LV_EVENT_PRESSED, NULL);

    /* A page per setting, the pager on the left (page coordinates: the tab starts at TAB_TOP); only the page
     * showing is built (ir_show_page). */
    const int32_t top = 68 - TAB_TOP, h = SCREEN_H - TAB_TOP - top - 4;
    s_ir_page = ui_box(page, SETTING_X, top, SCREEN_W - SETTING_X, h, C_BG);
    pager_create(&s_ir_pager, page, top, h, "Page");
    s_ir_pager.on_show = ir_show_page;
    pager_set_count(&s_ir_pager, NANO_CAB_PARAMS + 1, 0);
    /* Why there are no values, right of the captions on every page. */
    s_ir_hint = ui_label(page, &lv_font_montserrat_14, C_WARN);
    lv_obj_set_width(s_ir_hint, SCREEN_W - SETTING_X - 8);
    lv_obj_set_pos(s_ir_hint, SETTING_X, top);
    lv_obj_set_style_text_align(s_ir_hint, LV_TEXT_ALIGN_RIGHT, 0);
}

static void build_capture(lv_obj_t *scr)
{
    s_capture_view = make_overlay_cb(scr, "", on_close, on_close, NULL);
    /* Tabs between "<" and "x". */
    static const char *const names[2] = { "Capture", "IR" };
    const int32_t x0 = 48, w = (SCREEN_W - 2 * x0 - 6) / 2;
    for (int i = 0; i < 2; i++) {
        s_tab_btn[i] = ui_button(s_capture_view, x0 + i * (w + 6), 1, w, TOP_H + 2, names[i], &lv_font_montserrat_14, C_PANEL_2, C_TEXT, on_tab, NULL);
        lv_obj_set_style_radius(s_tab_btn[i], 6, 0);
        lv_obj_set_style_border_side(s_tab_btn[i], LV_BORDER_SIDE_BOTTOM, 0);
        lv_obj_set_style_border_color(s_tab_btn[i], lv_color_hex(C_ACCENT), 0);
        lv_obj_set_ext_click_area(s_tab_btn[i], 4);
        lv_obj_add_event_cb(s_tab_btn[i], ui_on_pressed, LV_EVENT_PRESSED, NULL);
    }
    /* Below the header, so the header stays tappable. */
    s_tab_page = ui_box(s_capture_view, 0, TAB_TOP, SCREEN_W, SCREEN_H - TAB_TOP, C_BG);
    s_tab_built = -1;
}

/*
 * Rename page: a ui_text_edit with the preset's name. The pedal stores a new name at once (no save
 * needed); the page waits for its answer (nano_ui_rename_result) and closes on success.
 */
static void on_rename_back(lv_event_t *e) { (void)e; nano_ui_show(s_rename_from); }

/* The pedal accepts any name; like DrD85's controller, no two presets share one (ignoring letter case).
 * Spaces at either end would be invisible on the screen. */
static bool rename_validate(const char *text, char *why, size_t cap, void *user)
{
    (void)user;
    if (text[0] == ' ' || text[strlen(text) - 1] == ' ') {
        snprintf(why, cap, "No space at the start or end");
        return false;
    }
    for (int i = 0; s_meta && i < NANO_PRESET_COUNT; i++) {
        if (i != s_rename_idx && strcasecmp(s_meta->presets[i].name, text) == 0) {
            snprintf(why, cap, "Preset %d already has this name", i + 1);
            return false;
        }
    }
    return true;
}

static void rename_submit(const char *text, void *user)
{
    (void)user;
    const char *old = s_meta ? s_meta->presets[s_rename_idx].name : "";
    if (strcmp(text, old) == 0 || !s_cb.on_rename_preset) {
        nano_ui_show(s_rename_from); /* nothing to write */
        return;
    }
    s_cb.on_rename_preset(s_rename_idx, text);
}

static void build_rename(lv_obj_t *scr)
{
    char label[8], title[32];
    nano_preset_label(s_rename_idx, s_per_bank, s_label_style, label, sizeof(label));
    snprintf(title, sizeof(title), "Rename preset %s", label);
    s_rename_view = make_overlay_cb(scr, title, on_rename_back, on_close, NULL);
    const ui_text_edit_cfg_t cfg = {
        .text = s_meta ? s_meta->presets[s_rename_idx].name : "",
        .placeholder = "Preset name",
        .min_len = 4, .max_len = NANO_PRESET_NAME_MAX,
        .validate = rename_validate,
        .on_submit = rename_submit,
    };
    s_rename_edit = ui_text_edit_create(s_rename_view, TOP_H + 8, &cfg);
}

void nano_ui_open_rename(uint8_t index)
{
    if (index >= NANO_PRESET_COUNT || !s_cb.on_rename_preset) return;
    s_rename_idx = index;
    s_rename_from = s_view == NANO_VIEW_PRESETS ? NANO_VIEW_PRESETS : s_base_view;
    show_view(NANO_VIEW_RENAME, true);
}

void nano_ui_rename_result(uint8_t index, bool ok, const char *msg)
{
    if (s_view != NANO_VIEW_RENAME || index != s_rename_idx) return;
    if (ok) nano_ui_show(s_rename_from);
    else ui_text_edit_set_error(s_rename_edit, msg);
}

static void on_preset_name_long(lv_event_t *e) { (void)e; nano_ui_open_rename(s_preset); }

static void build_tuner(lv_obj_t *scr)
{
    s_tuner = make_overlay(scr, "Tuner", true);
    s_tuner_note = ui_label(s_tuner, &lv_font_montserrat_40, C_TEXT);
    lv_obj_set_width(s_tuner_note, SCREEN_W);
    lv_obj_set_style_text_align(s_tuner_note, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(s_tuner_note, 0, 40);
    lv_label_set_text(s_tuner_note, "-");
    /* Cents bar: -50..+50 with a centre mark. */
    lv_obj_t *track = ui_box(s_tuner, 20, 110, SCREEN_W - 40, 14, C_PANEL_2);
    lv_obj_set_style_radius(track, 7, 0);
    lv_obj_t *centre = ui_box(s_tuner, SCREEN_W / 2 - 1, 100, 2, 34, C_MUTED);
    (void)centre;
    s_tuner_bar = ui_box(s_tuner, SCREEN_W / 2 - 6, 108, 12, 18, C_WARN);
    lv_obj_set_style_radius(s_tuner_bar, 6, 0);
    s_tuner_cents = ui_label(s_tuner, &lv_font_montserrat_20, C_MUTED);
    lv_obj_set_width(s_tuner_cents, SCREEN_W);
    lv_obj_set_style_text_align(s_tuner_cents, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(s_tuner_cents, 0, 140);
    lv_label_set_text(s_tuner_cents, "");
    s_tuner_verdict = ui_label(s_tuner, &lv_font_montserrat_14, C_MUTED);
    lv_obj_set_width(s_tuner_verdict, SCREEN_W);
    lv_obj_set_style_text_align(s_tuner_verdict, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(s_tuner_verdict, 0, 166);
    lv_label_set_text(s_tuner_verdict, "Play a note");
    /* Mute state, tappable: re-sends tuner-on with the other flag (Cortex Cloud does the same). */
    s_tuner_mute = ui_button(s_tuner, SCREEN_W - 44 - 96, 4, 92, 22, "SOUND ON", &montserrat_medium_10, C_PANEL, C_MUTED, on_mute_clicked, NULL);
    lv_obj_set_style_radius(s_tuner_mute, 7, 0);
    lv_obj_add_event_cb(s_tuner_mute, ui_on_pressed, LV_EVENT_PRESSED, NULL);
    lv_obj_set_ext_click_area(s_tuner_mute, 8);
    lv_obj_t *done = ui_button(s_tuner, 12, SCREEN_H - 50, SCREEN_W - 24, 40, "Done", &lv_font_montserrat_20, C_PANEL, C_TEXT, on_close, NULL);
    lv_obj_add_event_cb(done, ui_on_pressed, LV_EVENT_PRESSED, NULL);
}

static void build_tempo(lv_obj_t *scr)
{
    s_tempo_view = make_overlay(scr, "Tempo", true);
    s_tempo_big = ui_label(s_tempo_view, &lv_font_montserrat_40, C_ON);
    lv_obj_set_width(s_tempo_big, SCREEN_W);
    lv_obj_set_style_text_align(s_tempo_big, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(s_tempo_big, 0, 44);
    lv_label_set_text(s_tempo_big, "-");
    lv_obj_t *unit = ui_label(s_tempo_view, &lv_font_montserrat_14, C_MUTED);
    lv_obj_set_width(unit, SCREEN_W);
    lv_obj_set_style_text_align(unit, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(unit, 0, 92);
    lv_label_set_text(unit, "BPM");
    s_tempo_hint = ui_label(s_tempo_view, &lv_font_montserrat_12, C_MUTED);
    lv_obj_set_width(s_tempo_hint, SCREEN_W);
    lv_obj_set_style_text_align(s_tempo_hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(s_tempo_hint, 0, 112);
    lv_label_set_text(s_tempo_hint, "");
    /* Step on the press itself (not the release) and auto-repeat while held. */
    lv_obj_t *minus = ui_button(s_tempo_view, 12, 134, 140, 48, LV_SYMBOL_MINUS, &lv_font_montserrat_20, C_PANEL, C_TEXT, ui_on_pressed, NULL);
    lv_obj_remove_event_cb(minus, ui_on_pressed);
    lv_obj_add_event_cb(minus, ui_on_pressed, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(minus, on_tempo_step, LV_EVENT_PRESSED, (void *)(intptr_t)-1);
    lv_obj_add_event_cb(minus, on_tempo_step, LV_EVENT_LONG_PRESSED_REPEAT, (void *)(intptr_t)-1);
    lv_obj_t *plus = ui_button(s_tempo_view, SCREEN_W - 12 - 140, 134, 140, 48, LV_SYMBOL_PLUS, &lv_font_montserrat_20, C_PANEL, C_TEXT, ui_on_pressed, NULL);
    lv_obj_remove_event_cb(plus, ui_on_pressed);
    lv_obj_add_event_cb(plus, ui_on_pressed, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(plus, on_tempo_step, LV_EVENT_PRESSED, (void *)(intptr_t)1);
    lv_obj_add_event_cb(plus, on_tempo_step, LV_EVENT_LONG_PRESSED_REPEAT, (void *)(intptr_t)1);
    lv_obj_t *done = ui_button(s_tempo_view, 12, SCREEN_H - 50, SCREEN_W - 24, 40, "Done", &lv_font_montserrat_20, C_PANEL, C_TEXT, on_close, NULL);
    lv_obj_add_event_cb(done, ui_on_pressed, LV_EVENT_PRESSED, NULL);
}

/* ---- connect page --------------------------------------------------------- */

#define RING_D 62
#define RING_INNER_D 40
#define RING_GAP 44        /* room for the "+" between the rings */

static lv_obj_t *make_ring(lv_obj_t *parent, int32_t x, int32_t y, const char *caption)
{
    lv_obj_t *outer = ui_box(parent, x, y, RING_D, RING_D, C_PANEL);
    lv_obj_set_style_radius(outer, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(outer, 3, 0);
    lv_obj_set_style_border_color(outer, lv_color_hex(C_ON), 0);
    lv_obj_t *inner = ui_box(outer, 0, 0, RING_INNER_D, RING_INNER_D, 0x22B08A);
    lv_obj_set_style_radius(inner, LV_RADIUS_CIRCLE, 0);
    lv_obj_center(inner);
    lv_obj_t *l = ui_label(parent, &montserrat_medium_12, C_TEXT);
    lv_label_set_text(l, caption);
    lv_obj_set_style_text_letter_space(l, 2, 0);
    lv_obj_set_width(l, RING_D + 40);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(l, x - 20, y + RING_D + 6);
    return outer;
}

static void dot_opa_cb(void *obj, int32_t v) { lv_obj_set_style_opa((lv_obj_t *)obj, (lv_opa_t)v, 0); }

/* The status dot breathes while the page is showing (the only motion on a page that may sit for minutes). */
static void connect_dot_animate(bool run)
{
    lv_anim_delete(s_connect_dot, dot_opa_cb);
    lv_obj_set_style_opa(s_connect_dot, LV_OPA_COVER, 0);
    if (!run) return;
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_connect_dot);
    lv_anim_set_exec_cb(&a, dot_opa_cb);
    lv_anim_set_values(&a, LV_OPA_COVER, LV_OPA_20);
    lv_anim_set_duration(&a, 700);
    lv_anim_set_playback_duration(&a, 700);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&a);
}

static void build_connect(lv_obj_t *scr)
{
    s_connect = ui_box(scr, 0, 0, SCREEN_W, SCREEN_H, C_BG);
    lv_obj_add_event_cb(s_connect, ui_on_pressed, LV_EVENT_PRESSED, NULL);
    lv_obj_t *menu = ui_button(s_connect, SCREEN_W - 44, 0, 44, TOP_H + 4, LV_SYMBOL_LIST, &lv_font_montserrat_14, C_BG, C_MUTED, on_menu, NULL);
    lv_obj_add_event_cb(menu, ui_on_pressed, LV_EVENT_PRESSED, NULL);
    lv_obj_set_ext_click_area(menu, 6);

    s_connect_title = ui_label(s_connect, &lv_font_montserrat_14, C_TEXT);
    lv_obj_set_width(s_connect_title, SCREEN_W - 88);
    lv_obj_set_style_text_align(s_connect_title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(s_connect_title, 44, 28);
    lv_label_set_text(s_connect_title, "Put the pedal in connect mode");

    /* Scanning: the pairing gesture (EXIT + CAPTURE), as Cortex Cloud shows it. */
    s_connect_pair = ui_box(s_connect, 0, 50, SCREEN_W, 150, C_BG);
    const int32_t left_x = SCREEN_W / 2 - RING_GAP / 2 - RING_D;
    const int32_t right_x = SCREEN_W / 2 + RING_GAP / 2;
    make_ring(s_connect_pair, left_x, 0, "EXIT");
    make_ring(s_connect_pair, right_x, 0, "CAPTURE");
    lv_obj_t *plus = ui_label(s_connect_pair, &lv_font_montserrat_24, C_MUTED);
    lv_label_set_text(plus, "+");
    lv_obj_set_width(plus, RING_GAP);
    lv_obj_set_style_text_align(plus, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(plus, SCREEN_W / 2 - RING_GAP / 2, RING_D / 2 - 14);
    lv_obj_t *hold = ui_label(s_connect_pair, &lv_font_montserrat_14, C_TEXT);
    lv_label_set_text(hold, "Hold both for 2 seconds");
    lv_obj_set_width(hold, SCREEN_W);
    lv_obj_set_style_text_align(hold, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(hold, 0, RING_D + 30);
    lv_obj_t *note = ui_label(s_connect_pair, &lv_font_montserrat_12, C_MUTED);
    lv_label_set_text(note, "The pedal pairs with one device at a time: close Cortex Cloud on your phone first.");
    lv_label_set_long_mode(note, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(note, SCREEN_W - 40);
    lv_obj_set_style_text_align(note, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(note, 20, RING_D + 54);

    /* Disconnected on purpose: one button brings the link back. */
    s_connect_btn = ui_button(s_connect, SCREEN_W / 2 - 80, 96, 160, 48, LV_SYMBOL_BLUETOOTH "  Connect", &lv_font_montserrat_20, C_ACCENT, C_FX_TEXT, on_connect_clicked, NULL);
    lv_obj_add_event_cb(s_connect_btn, ui_on_pressed, LV_EVENT_PRESSED, NULL);
    lv_obj_set_hidden(s_connect_btn, true);
    s_connect_free = ui_label(s_connect, &lv_font_montserrat_12, C_MUTED);
    lv_label_set_text(s_connect_free, "Cortex Cloud can use the pedal now");
    lv_obj_set_width(s_connect_free, SCREEN_W);
    lv_obj_set_style_text_align(s_connect_free, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(s_connect_free, 0, 96 + 48 + 14);
    lv_obj_set_hidden(s_connect_free, true);

    s_connect_status = ui_label(s_connect, &lv_font_montserrat_14, C_MUTED);
    lv_obj_align(s_connect_status, LV_ALIGN_BOTTOM_MID, 8, -12);
    s_connect_dot = ui_dot(s_connect, 0, 0, 10);
    lv_obj_set_style_bg_color(s_connect_dot, lv_color_hex(C_WARN), 0);
    lv_obj_set_hidden(s_connect, true);
}

static void refresh_tempo_views(void)
{
    if (s_tempo_bpm <= 0) {
        lv_label_set_text(s_tempo, "");
        lv_label_set_text(s_tempo_big, "-");
        return;
    }
    char t[20];
    snprintf(t, sizeof(t), s_tempo_tapping ? "TAP %d" : "%d BPM", (int)(s_tempo_bpm + 0.5f));
    lv_label_set_text(s_tempo, t);
    uint32_t c = s_tempo_tapping ? C_WARN : C_ON;
    lv_obj_set_style_text_color(s_tempo, lv_color_hex(c), 0);
    snprintf(t, sizeof(t), "%d", (int)(s_tempo_bpm + 0.5f));
    lv_label_set_text(s_tempo_big, t);
    lv_obj_set_style_text_color(s_tempo_big, lv_color_hex(c), 0);
    lv_label_set_text(s_tempo_hint, s_tempo_tapping ? "Tap tempo on the pedal" : "");
}

/* ---- public --------------------------------------------------------------- */

void nano_ui_create(lv_display_t *disp, const nano_ui_callbacks_t *cb)
{
    s_cb = *cb;
    lv_obj_t *scr = lv_display_get_screen_active(disp);
    s_scr = scr;
    lv_obj_set_style_bg_color(scr, lv_color_hex(C_BG), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_scrollable(scr, false);
    build_main(scr);
    build_menu(scr);
    build_settings(scr);
    build_tuner(scr);
    build_tempo(scr);
    build_connect(scr);
    nano_ui_set_link_enabled(true);
    nano_ui_set_status("Starting", false);
    nano_ui_set_connected(false);
}

static void build_update(lv_obj_t *scr);

static void show_view(nano_view_t view, bool notify)
{
    if (view == s_view) return;
    /* Only the user leaves the update view (it ends in a restart); late pedal events must not. */
    if (s_view == NANO_VIEW_UPDATE && !notify) return;
    if (view == NANO_VIEW_UPDATE && !s_update) build_update(s_scr);
    if (s_view == NANO_VIEW_UPDATE && notify && s_cb.on_update_close) s_cb.on_update_close();
    if (s_view == NANO_VIEW_TUNER && notify && s_cb.on_tuner) s_cb.on_tuner(false);
    if (s_view == NANO_VIEW_TEMPO && notify && s_cb.on_tempo_view) s_cb.on_tempo_view(false);
    nano_view_t prev = s_view;
    s_view = view;
    lv_obj_set_hidden(s_menu, view != NANO_VIEW_MENU);
    lv_obj_set_hidden(s_settings, view != NANO_VIEW_SETTINGS);
    if (view == NANO_VIEW_PRESETS) {
        build_presets(s_scr);
        presets_open();
        lv_obj_set_hidden(s_presets, false);
    } else if (s_presets) {
        /* Async: the tap that closes it is still being handled by one of its children. */
        lv_obj_delete_async(s_presets);
        s_presets = NULL;
    }
    if (view == NANO_VIEW_CAPTURE || view == NANO_VIEW_IR) {
        if (!s_capture_view) build_capture(s_scr);
        tabs_select(view == NANO_VIEW_IR);
        capture_refresh();
        ir_refresh();
        lv_obj_set_hidden(s_capture_view, false);
    } else if (s_capture_view) {
        lv_obj_delete_async(s_capture_view);
        s_capture_view = NULL;
        s_tab_built = -1;
        s_cap_vol = NULL; /* freed with the page */
        memset(s_ir_ctrl, 0, sizeof(s_ir_ctrl));
        memset(s_ir_mic_btn, 0, sizeof(s_ir_mic_btn));
        memset(s_ir_pos_btn, 0, sizeof(s_ir_pos_btn));
        s_ir_mic_note = NULL;
    }
    /* The app reads the IR settings while their tab shows (after the tab exists: the answer lands in it). */
    if ((prev == NANO_VIEW_IR) != (view == NANO_VIEW_IR) && s_cb.on_ir_view) s_cb.on_ir_view(view == NANO_VIEW_IR);
    if (view == NANO_VIEW_RENAME) {
        build_rename(s_scr);
        lv_obj_set_hidden(s_rename_view, false);
    } else if (s_rename_view) {
        lv_obj_delete_async(s_rename_view);
        s_rename_view = NULL;
        s_rename_edit = NULL; /* freed with the page */
    }
    lv_obj_set_hidden(s_tuner, view != NANO_VIEW_TUNER);
    lv_obj_set_hidden(s_tempo_view, view != NANO_VIEW_TEMPO);
    lv_obj_set_hidden(s_connect, view != NANO_VIEW_CONNECT);
    if (s_update) lv_obj_set_hidden(s_update, view != NANO_VIEW_UPDATE);
    connect_dot_animate(view == NANO_VIEW_CONNECT && s_link_enabled);
    if (view == NANO_VIEW_TUNER) {
        nano_ui_set_tuner(NULL, 0, false);
        if (notify && s_cb.on_tuner) s_cb.on_tuner(true);
    }
    if (view == NANO_VIEW_TEMPO && notify && s_cb.on_tempo_view) s_cb.on_tempo_view(true);
    if (view == NANO_VIEW_UPDATE && notify && s_cb.on_update_open) s_cb.on_update_open();
}

void nano_ui_open_tempo_from_pedal(void)
{
    show_view(NANO_VIEW_TEMPO, false);
}

void nano_ui_close_from_pedal(void)
{
    show_view(s_base_view, false);
}

void nano_ui_set_connected(bool live)
{
    s_base_view = live ? NANO_VIEW_MAIN : NANO_VIEW_CONNECT;
    if (live && s_view == NANO_VIEW_CONNECT) show_view(NANO_VIEW_MAIN, false);
    /* Down: the pedal's views make no claims any more; the menu and settings can stay open. */
    if (!live && (s_view == NANO_VIEW_MAIN || s_view == NANO_VIEW_TUNER || s_view == NANO_VIEW_TEMPO || s_view == NANO_VIEW_CAPTURE || s_view == NANO_VIEW_IR || s_view == NANO_VIEW_RENAME)) show_view(NANO_VIEW_CONNECT, false);
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
    lv_label_set_text(s_connect_status, text);
    lv_obj_set_style_bg_color(s_connect_dot, lv_color_hex(connected ? C_ON : s_link_enabled ? C_WARN : C_DIM), 0);
    lv_obj_update_layout(s_connect_status);
    lv_obj_align_to(s_connect_dot, s_connect_status, LV_ALIGN_OUT_LEFT_MID, -8, 0);
}

void nano_ui_set_link_enabled(bool enabled)
{
    s_link_enabled = enabled;
    lv_label_set_text(s_link_btn_label, enabled ? LV_SYMBOL_BLUETOOTH "  Disconnect" : LV_SYMBOL_BLUETOOTH "  Connect");
    lv_obj_set_style_text_color(s_link_btn_label, lv_color_hex(enabled ? C_ERROR : C_TEXT), 0);
    lv_obj_center(s_link_btn_label);
    /* Connect page: the pairing gesture while scanning, a Connect button after a deliberate disconnect. */
    lv_label_set_text(s_connect_title, enabled ? "Put the pedal in connect mode" : "Disconnected from the pedal");
    lv_obj_set_hidden(s_connect_pair, !enabled);
    lv_obj_set_hidden(s_connect_btn, enabled);
    lv_obj_set_hidden(s_connect_free, enabled);
    connect_dot_animate(s_view == NANO_VIEW_CONNECT && enabled);
}

void nano_ui_set_bank_size(uint8_t per_bank)
{
    if (per_bank < 2 || per_bank > 8) return;
    s_per_bank = per_bank;
    set_bank_value_text();
}

void nano_ui_set_label_style(uint8_t style)
{
    if (style > NANO_LABEL_NUMERIC) return;
    s_label_style = (nano_label_style_t)style;
    refresh_style_seg();
}

void nano_ui_set_outputs_muted(bool muted)
{
    s_outputs_muted = muted;
    refresh_mute();
}

void nano_ui_settings_page(int index)
{
    pager_show(&s_settings_pager, index);
}

void nano_ui_set_rotation(bool rotate_180)
{
    s_rot180 = rotate_180;
    refresh_rot_seg();
}

void nano_ui_set_brightness(uint8_t level)
{
    if (level < BRIGHTNESS_MIN || level > BRIGHTNESS_MAX) return;
    s_brightness = level;
    set_brightness_text();
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
    int32_t w = RIGHT_X - NAV_W - 4 - x;
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
    s_meta = meta;
    if (s_view == NANO_VIEW_PRESETS) presets_show_bank(s_presets_pager.current);
    char label[8];
    nano_preset_label(index, s_per_bank, s_label_style, label, sizeof(label));
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
    capture_from_state(st);
    set_line(s_ir_dot, s_ir, st->ir_short_name, st->cab_on, "No IR");
    snprintf(s_ir_name, sizeof(s_ir_name), "%s", st->ir_short_name);
    s_ir_on = st->cab_on;
    if (s_ir_owner != st->active_preset) s_ir_known = false; /* the app reads the new preset's settings */
    ir_refresh();
    s_gate_on = st->gate_on;
    lv_obj_set_style_bg_color(s_gate, lv_color_hex(st->gate_on ? nano_category_color(NANO_CAT_UTILITY) : C_OFF), 0);
    lv_obj_set_style_text_color(lv_obj_get_child(s_gate, 0), lv_color_hex(st->gate_on ? C_FX_TEXT : C_TEXT), 0);
    nano_ui_set_tempo(st->tempo_bpm, false);
    for (int i = 0; i < NANO_FX_SLOT_COUNT; i++) {
        const nano_fx_slot_t *fx = &st->fx[i];
        bool present = fx->id[0] != 0;
        bool on = present && st->has_bypass && st->fx_on[i];
        s_tile_present[i] = present;
        s_tile_on[i] = on;
        nano_category_t cat = fx->model ? fx->model->category : NANO_CAT_UNKNOWN;
        uint32_t color = nano_category_color(cat);
        s_tile_color[i] = color;
        const char *name = fx->model ? fx->model->name : (present ? fx->id : "");
        lv_label_set_text(s_tile_names[i], name);
        lv_obj_set_style_text_font(s_tile_names[i], tile_font(name, TILE_W - 4), 0);
        lv_label_set_text(s_tile_tags[i], present ? nano_category_short(cat) : "");
        if (!present) {
            lv_obj_set_style_bg_color(s_tiles[i], lv_color_hex(C_BG), 0);
            lv_obj_set_style_border_color(s_tiles[i], lv_color_hex(0x262D37), 0);
            lv_obj_set_style_text_color(s_tile_names[i], lv_color_hex(C_DIM), 0);
        } else if (on) {
            uint32_t text = nano_category_light_text(cat) ? C_TEXT : C_FX_TEXT;
            lv_obj_set_style_bg_color(s_tiles[i], lv_color_hex(color), 0);
            lv_obj_set_style_border_color(s_tiles[i], lv_color_hex(color), 0);
            lv_obj_set_style_text_color(s_tile_names[i], lv_color_hex(text), 0);
            lv_obj_set_style_text_color(s_tile_tags[i], lv_color_mix(lv_color_hex(text), lv_color_hex(color), 170), 0);
        } else {
            lv_obj_set_style_bg_color(s_tiles[i], lv_color_hex(C_OFF), 0);
            lv_obj_set_style_border_color(s_tiles[i], lv_color_mix(lv_color_hex(color), lv_color_hex(C_OFF), 140), 0);
            lv_obj_set_style_text_color(s_tile_names[i], lv_color_hex(C_TEXT), 0);
            /* Off: the tag carries the category colour, a touch brighter than the border. */
            lv_obj_set_style_text_color(s_tile_tags[i], lv_color_mix(lv_color_hex(color), lv_color_hex(C_OFF), 190), 0);
        }
    }
    refresh_expression();
}

void nano_ui_ir_page(int index)
{
    if (s_capture_view && s_tab_built == 1) pager_show(&s_ir_pager, index);
}

void nano_ui_set_ir_settings(const nano_cab_settings_t *settings, int preset, bool fresh)
{
    s_ir_owner = preset;
    s_ir_known = settings != NULL;
    if (settings) s_ir_set = *settings;
    ir_refresh_as(fresh);
}

void nano_ui_set_tempo(float bpm, bool tapping)
{
    s_tempo_bpm = bpm;
    s_tempo_tapping = tapping;
    refresh_tempo_views();
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
    lv_obj_set_style_opa(s_list_btn, opa, 0);
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

/* ---- firmware update view --------------------------------------------------- */

#define UPD_X 12
#define UPD_W (SCREEN_W - 2 * UPD_X)
#define UPD_ROW_H 36
#define UPD_PASS_TA_H 36
#define UPD_KB_Y (TOP_H + 4 + 4 + UPD_PASS_TA_H + 22)

typedef enum { UPD_PANEL_MAIN, UPD_PANEL_NETWORKS, UPD_PANEL_PASSWORD } upd_panel_t;

static upd_panel_t s_upd_panel = UPD_PANEL_MAIN;

/* The header "<": password -> networks, networks -> the update (only with a network to go back to). */
static void upd_refresh_back(void)
{
    bool back = s_upd_panel == UPD_PANEL_PASSWORD || (s_upd_panel == UPD_PANEL_NETWORKS && s_wifi_ssid[0]);
    lv_obj_set_hidden(s_upd_back, !back);
    lv_obj_set_x(s_upd_title, back ? 40 : 12);
}

static void upd_show_panel(upd_panel_t p)
{
    s_upd_panel = p;
    upd_refresh_back();
    lv_obj_set_hidden(s_upd_main, p != UPD_PANEL_MAIN);
    lv_obj_set_hidden(s_upd_nets, p != UPD_PANEL_NETWORKS);
    lv_obj_set_hidden(s_upd_pass, p != UPD_PANEL_PASSWORD);
}

static void upd_refresh_wifi(void)
{
    lv_label_set_text(s_upd_wifi, s_wifi_ssid[0] ? s_wifi_ssid : "Not set up");
    lv_obj_set_style_text_color(s_upd_wifi, lv_color_hex(s_wifi_ssid[0] ? C_TEXT : C_MUTED), 0);
    upd_refresh_back();
}

static void upd_start_join(const char *ssid, const char *password)
{
    strncpy(s_wifi_ssid, ssid, sizeof(s_wifi_ssid) - 1);
    s_wifi_ssid[sizeof(s_wifi_ssid) - 1] = '\0';
    upd_refresh_wifi();
    char t[64];
    snprintf(t, sizeof(t), "Connecting to %s", ssid);
    nano_ui_update_status(NANO_UPDATE_BUSY, t, 0);
    upd_show_panel(UPD_PANEL_MAIN);
    if (s_cb.on_wifi_join) s_cb.on_wifi_join(ssid, password);
}

static void on_upd_close(lv_event_t *e) { (void)e; nano_ui_show(s_base_view); }

static void on_upd_change_wifi(lv_event_t *e)
{
    (void)e;
    nano_ui_update_show_networks(NULL, 0, true);
    if (s_cb.on_wifi_scan) s_cb.on_wifi_scan();
}

static void on_upd_back(lv_event_t *e)
{
    (void)e;
    upd_show_panel(s_upd_panel == UPD_PANEL_PASSWORD ? UPD_PANEL_NETWORKS : UPD_PANEL_MAIN);
}

static void on_upd_action(lv_event_t *e)
{
    (void)e;
    if (s_upd_state == NANO_UPDATE_AVAILABLE) {
        nano_ui_update_status(NANO_UPDATE_DOWNLOADING, NULL, 0);
        if (s_cb.on_update_install) s_cb.on_update_install();
    } else if (s_upd_state == NANO_UPDATE_UP_TO_DATE || s_upd_state == NANO_UPDATE_ERROR) {
        nano_ui_update_status(NANO_UPDATE_BUSY, "Checking for updates", 0);
        if (s_cb.on_update_check) s_cb.on_update_check();
    }
}

static void on_upd_network(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (i < 0 || i >= s_upd_network_count) return;
    const nano_ui_network_t *n = &s_upd_networks[i];
    if (!n->secure) {
        upd_start_join(n->ssid, "");
        return;
    }
    strncpy(s_upd_pick, n->ssid, sizeof(s_upd_pick) - 1);
    s_upd_pick[sizeof(s_upd_pick) - 1] = '\0';
    char t[64];
    snprintf(t, sizeof(t), "Password for %s", n->ssid);
    lv_label_set_text(s_upd_pass_title, t);
    lv_textarea_set_text(s_upd_pass_ta, "");
    upd_show_panel(UPD_PANEL_PASSWORD);
}

static void on_upd_eye(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target_obj(e);
    bool hidden = !lv_textarea_get_password_mode(s_upd_pass_ta);
    lv_textarea_set_password_mode(s_upd_pass_ta, hidden);
    lv_label_set_text(lv_obj_get_child(btn, 0), hidden ? LV_SYMBOL_EYE_OPEN : LV_SYMBOL_EYE_CLOSE);
}

static void on_upd_keyboard(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_READY) {
        const char *pw = lv_textarea_get_text(s_upd_pass_ta);
        if (strlen(pw) < 8) return; /* WPA2 needs 8 or more: keep typing */
        upd_start_join(s_upd_pick, pw);
    }
}



/* Four bars, lit by signal strength. */
static void make_bars(lv_obj_t *parent, int32_t x, int32_t y, int8_t rssi)
{
    int lit = rssi > -55 ? 4 : rssi > -65 ? 3 : rssi > -75 ? 2 : 1;
    for (int i = 0; i < 4; i++) {
        int32_t h = 4 + i * 3;
        lv_obj_t *b = ui_box(parent, x + i * 5, y + 13 - h, 3, h, i < lit ? C_TEXT : C_DIM);
        lv_obj_set_clickable(b, false);
    }
}

static void build_update(lv_obj_t *scr)
{
    s_update = make_overlay_cb(scr, "Firmware update", on_upd_back, on_upd_close, &s_upd_close);
    s_upd_back = lv_obj_get_child(s_update, 0);  /* make_overlay_cb: back, title, close */
    s_upd_title = lv_obj_get_child(s_update, 1);
    const int32_t top = TOP_H + 4;

    /* Main panel: installed version, Wi-Fi line, status, action. */
    s_upd_main = ui_box(s_update, 0, top, SCREEN_W, SCREEN_H - top, C_BG);
    lv_obj_t *l = ui_label(s_upd_main, &lv_font_montserrat_12, C_MUTED);
    lv_label_set_text(l, "Installed");
    lv_obj_set_pos(l, UPD_X, 8);
    s_upd_version = ui_label(s_upd_main, &lv_font_montserrat_14, C_TEXT);
    lv_obj_set_pos(s_upd_version, 76, 6);
    lv_obj_set_width(s_upd_version, UPD_W - 64);
    lv_label_set_long_mode(s_upd_version, LV_LABEL_LONG_DOT);
    lv_label_set_text(s_upd_version, s_fw_version);
    l = ui_label(s_upd_main, &lv_font_montserrat_12, C_MUTED);
    lv_label_set_text(l, "Wi-Fi");
    lv_obj_set_pos(l, UPD_X, 38);
    s_upd_wifi = ui_label(s_upd_main, &lv_font_montserrat_14, C_TEXT);
    lv_obj_set_pos(s_upd_wifi, 76, 36);
    lv_obj_set_width(s_upd_wifi, UPD_W - 64 - 84);
    lv_label_set_long_mode(s_upd_wifi, LV_LABEL_LONG_DOT);
    lv_obj_t *change = ui_button(s_upd_main, SCREEN_W - UPD_X - 76, 30, 76, 28, "Change", &lv_font_montserrat_12, C_PANEL, C_ACCENT, on_upd_change_wifi, NULL);
    lv_obj_set_style_radius(change, 7, 0);
    lv_obj_add_event_cb(change, ui_on_pressed, LV_EVENT_PRESSED, NULL);
    lv_obj_set_ext_click_area(change, 6);
    ui_box(s_upd_main, UPD_X, 66, UPD_W, 1, C_PANEL_2);

    s_upd_text = ui_label(s_upd_main, &lv_font_montserrat_20, C_TEXT);
    lv_obj_set_width(s_upd_text, UPD_W);
    lv_obj_set_pos(s_upd_text, UPD_X, 78);
    lv_obj_set_style_text_align(s_upd_text, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s_upd_text, LV_LABEL_LONG_DOT);
    lv_obj_set_height(s_upd_text, lv_font_get_line_height(&lv_font_montserrat_20));
    s_upd_sub = ui_label(s_upd_main, &lv_font_montserrat_12, C_MUTED);
    lv_obj_set_width(s_upd_sub, UPD_W);
    lv_obj_set_pos(s_upd_sub, UPD_X, 106);
    lv_obj_set_style_text_align(s_upd_sub, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s_upd_sub, LV_LABEL_LONG_WRAP);
    s_upd_bar = lv_bar_create(s_upd_main);
    lv_obj_set_pos(s_upd_bar, UPD_X + 12, 136);
    lv_obj_set_size(s_upd_bar, UPD_W - 24, 10);
    lv_bar_set_range(s_upd_bar, 0, 100);
    lv_obj_set_style_bg_color(s_upd_bar, lv_color_hex(C_PANEL_2), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_upd_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_upd_bar, lv_color_hex(C_ON), LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_upd_bar, 5, LV_PART_MAIN);
    lv_obj_set_style_radius(s_upd_bar, 5, LV_PART_INDICATOR);
    s_upd_action = ui_button(s_upd_main, UPD_X, SCREEN_H - top - 54, UPD_W, 44, "", &lv_font_montserrat_20, C_ACCENT, C_FX_TEXT, on_upd_action, NULL);
    lv_obj_add_event_cb(s_upd_action, ui_on_pressed, LV_EVENT_PRESSED, NULL);

    /* Network picker. */
    s_upd_nets = ui_box(s_update, 0, top, SCREEN_W, SCREEN_H - top, C_BG);
    s_upd_nets_title = ui_label(s_upd_nets, &lv_font_montserrat_14, C_TEXT);
    lv_label_set_text(s_upd_nets_title, "Choose Wi-Fi");
    lv_obj_set_pos(s_upd_nets_title, UPD_X, 8);
    lv_obj_t *rescan = ui_button(s_upd_nets, SCREEN_W - UPD_X - 76, 2, 76, 28, LV_SYMBOL_REFRESH " Scan", &lv_font_montserrat_12, C_PANEL, C_ACCENT, on_upd_change_wifi, NULL);
    lv_obj_set_style_radius(rescan, 7, 0);
    lv_obj_add_event_cb(rescan, ui_on_pressed, LV_EVENT_PRESSED, NULL);
    s_upd_net_list = ui_box(s_upd_nets, UPD_X, 36, UPD_W, SCREEN_H - top - 40, C_BG);
    lv_obj_set_scrollable(s_upd_net_list, true);
    lv_obj_set_scroll_dir(s_upd_net_list, LV_DIR_VER);
    lv_obj_set_flex_flow(s_upd_net_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_upd_net_list, 4, 0);

    /* Password: field + show/hide, keyboard under it (OK joins, the header "<" goes back). */
    s_upd_pass = ui_box(s_update, 0, top, SCREEN_W, SCREEN_H - top, C_BG);
    s_upd_pass_title = ui_label(s_upd_pass, &lv_font_montserrat_12, C_MUTED);
    lv_obj_set_pos(s_upd_pass_title, UPD_X, 2);
    lv_obj_set_size(s_upd_pass_title, UPD_W, lv_font_get_line_height(&lv_font_montserrat_12));
    lv_label_set_long_mode(s_upd_pass_title, LV_LABEL_LONG_DOT);
    s_upd_pass_ta = ui_text_field(s_upd_pass, UPD_X, 20, UPD_W - 48, UPD_PASS_TA_H, &lv_font_montserrat_14);
    lv_textarea_set_password_mode(s_upd_pass_ta, true);
    lv_textarea_set_max_length(s_upd_pass_ta, 63);
    lv_textarea_set_placeholder_text(s_upd_pass_ta, "8 characters or more");
    lv_obj_t *eye = ui_button(s_upd_pass, SCREEN_W - UPD_X - 42, 20, 42, UPD_PASS_TA_H, LV_SYMBOL_EYE_OPEN, &lv_font_montserrat_14, C_PANEL, C_MUTED, on_upd_eye, NULL);
    lv_obj_set_style_radius(eye, 7, 0);
    s_upd_kb = ui_keyboard(s_upd_pass, SCREEN_H - UPD_KB_Y, s_upd_pass_ta);
    lv_obj_add_event_cb(s_upd_kb, on_upd_keyboard, LV_EVENT_READY, NULL);

    upd_refresh_wifi();
    nano_ui_update_status(NANO_UPDATE_BUSY, "Starting Wi-Fi", 0);
    upd_show_panel(UPD_PANEL_MAIN);
}

void nano_ui_set_firmware_version(const char *version)
{
    strncpy(s_fw_version, version && version[0] ? version : "unknown", sizeof(s_fw_version) - 1);
    s_fw_version[sizeof(s_fw_version) - 1] = '\0';
    if (s_fw_value) lv_label_set_text(s_fw_value, s_fw_version);
    if (s_upd_version) lv_label_set_text(s_upd_version, s_fw_version);
}

void nano_ui_update_set_wifi(const char *ssid)
{
    strncpy(s_wifi_ssid, ssid ? ssid : "", sizeof(s_wifi_ssid) - 1);
    s_wifi_ssid[sizeof(s_wifi_ssid) - 1] = '\0';
    if (s_update) upd_refresh_wifi();
}

void nano_ui_update_status(nano_update_state_t state, const char *text, int percent)
{
    if (!s_update) return;
    s_upd_state = state;
    char t[64] = "", sub[96] = "";
    const char *action = NULL;
    uint32_t color = C_TEXT;
    switch (state) {
    case NANO_UPDATE_BUSY:
        snprintf(t, sizeof(t), "%s", text && text[0] ? text : "Working");
        snprintf(sub, sizeof(sub), "Bluetooth is off until you close this page");
        color = C_MUTED;
        break;
    case NANO_UPDATE_UP_TO_DATE:
        snprintf(t, sizeof(t), LV_SYMBOL_OK " Up to date");
        snprintf(sub, sizeof(sub), "Latest release: %s", text ? text : "");
        color = C_ON;
        action = "Check again";
        break;
    case NANO_UPDATE_AVAILABLE:
        snprintf(t, sizeof(t), "Update available");
        snprintf(sub, sizeof(sub), "Version %s", text ? text : "");
        color = C_ACCENT;
        action = LV_SYMBOL_DOWNLOAD "  Install";
        break;
    case NANO_UPDATE_DOWNLOADING:
        snprintf(t, sizeof(t), "Installing  %d%%", percent);
        snprintf(sub, sizeof(sub), "Keep the screen powered");
        break;
    case NANO_UPDATE_DONE:
        snprintf(t, sizeof(t), LV_SYMBOL_OK " Installed");
        snprintf(sub, sizeof(sub), "Restarting");
        color = C_ON;
        break;
    case NANO_UPDATE_ERROR:
        snprintf(t, sizeof(t), "%s", text && text[0] ? text : "Something went wrong");
        snprintf(sub, sizeof(sub), "Check the Wi-Fi network, then try again");
        color = C_ERROR;
        action = "Try again";
        break;
    }
    lv_label_set_text(s_upd_text, t);
    lv_obj_set_style_text_color(s_upd_text, lv_color_hex(color), 0);
    lv_label_set_text(s_upd_sub, sub);
    lv_obj_set_hidden(s_upd_bar, state != NANO_UPDATE_DOWNLOADING && state != NANO_UPDATE_DONE);
    lv_bar_set_value(s_upd_bar, state == NANO_UPDATE_DONE ? 100 : percent, LV_ANIM_OFF);
    lv_obj_set_hidden(s_upd_action, action == NULL);
    if (action) {
        lv_obj_t *al = lv_obj_get_child(s_upd_action, 0);
        lv_label_set_text(al, action);
        lv_obj_center(al);
    }
    /* Closing restarts the screen: not while the new image is being written or activated. */
    lv_obj_set_hidden(s_upd_close, state == NANO_UPDATE_DOWNLOADING || state == NANO_UPDATE_DONE);
    /* A check that was already running reports here while the user picks a network: it updates the
     * main panel behind the list instead of pulling the list away (the "Change" flicker). */
}

void nano_ui_update_show_networks(const nano_ui_network_t *networks, int count, bool scanning)
{
    if (!s_update) return;
    /* A scan in progress: the title says so and the last list stays (no blank, no rebuild). The tap and
     * the scan's own start both report it: the second one changes nothing. */
    lv_label_set_text(s_upd_nets_title, scanning ? "Searching..." : "Choose Wi-Fi");
    bool had_list = s_upd_network_count > 0;
    if (scanning) {
        bool again = s_upd_scanning;
        s_upd_scanning = true;
        if (s_upd_panel != UPD_PANEL_PASSWORD) upd_show_panel(UPD_PANEL_NETWORKS);
        if (again || had_list) return;
    } else {
        s_upd_scanning = false;
    }
    if (count > (int)(sizeof(s_upd_networks) / sizeof(s_upd_networks[0]))) count = (int)(sizeof(s_upd_networks) / sizeof(s_upd_networks[0]));
    s_upd_network_count = scanning || !networks ? 0 : count;
    if (s_upd_network_count) memcpy(s_upd_networks, networks, sizeof(*networks) * (size_t)s_upd_network_count);
    lv_obj_clean(s_upd_net_list);
    if (!s_upd_network_count) {
        lv_obj_t *l = ui_label(s_upd_net_list, &lv_font_montserrat_14, C_MUTED);
        lv_label_set_text(l, scanning ? "Searching for networks" : "No networks found");
    }
    for (int i = 0; i < s_upd_network_count; i++) {
        const nano_ui_network_t *n = &s_upd_networks[i];
        lv_obj_t *row = ui_button(s_upd_net_list, 0, 0, UPD_W, UPD_ROW_H, "", &lv_font_montserrat_14, C_PANEL, C_TEXT, on_upd_network, (void *)(intptr_t)i);
        lv_obj_set_style_radius(row, 8, 0);
        lv_obj_add_event_cb(row, ui_on_pressed, LV_EVENT_PRESSED, NULL);
        lv_obj_t *name = lv_obj_get_child(row, 0);
        lv_label_set_text(name, n->ssid);
        lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
        lv_obj_set_size(name, UPD_W - 70, lv_font_get_line_height(&lv_font_montserrat_14)); /* one line: LONG_DOT needs a fixed height */
        lv_obj_align(name, LV_ALIGN_LEFT_MID, 10, 0);
        if (n->secure) {
            lv_obj_t *lock = ui_label(row, &montserrat_medium_10, C_MUTED);
            lv_label_set_text(lock, "WPA");
            lv_obj_align(lock, LV_ALIGN_RIGHT_MID, -32, 0);
        }
        make_bars(row, UPD_W - 28, (UPD_ROW_H - 13) / 2, n->rssi);
    }
    if (s_upd_panel != UPD_PANEL_PASSWORD) upd_show_panel(UPD_PANEL_NETWORKS); /* never from under the typing */
}
