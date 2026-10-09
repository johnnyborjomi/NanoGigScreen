/*
 * IR tab: name with its on / off dot (teal while it is the IR the preset was saved with; hold it for the IR list), then a page per setting of
 * Cortex Cloud's IR loader: Level (under a row of On / Off, Phase (Ø) and < > through the pedal's IR list), High pass,
 * Low pass, then microphone and position (factory IRs only). The sliders run on the pedal's own 0..1, like Cortex
 * Cloud's; the steps count in readout units: Level tenths of a dB (raw = tenths + 960), High pass Hz, Low pass Hz
 * with 100 Hz steps. Only the page showing is built.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "ir_tab.h"
#include "ui_internal.h"
#include "ui_value_ctrl.h"
#include "ui_widgets.h"

#define IR_LEVEL_ZERO 960 /* raw of 0.0 dB: -96.0 dB = raw 0 */
#define MIC_BUTTONS 6     /* Cortex Cloud offers five */
#define PAGES (NANO_CAB_PARAMS + 1)
#define IR_BUTTON_H 32
#define NAME_X 34
#define NAME_W (SCREEN_W - NAME_X - 8)

/* The IR as the last state showed it, and its settings as the pedal last reported them. */
static struct {
    char name[NANO_NAME_CAP];
    bool on;
    nano_cab_settings_t set;   /* the pedal's last answer (values, microphone, position) */
    bool known;                /* `set` holds the pedal's answer for `owner` */
    bool unreadable;           /* the pedal answered for `owner`, but with no IR settings */
    int owner;                 /* preset the settings belong to */
    int preset;                /* preset the name belongs to */
} s = { .owner = -1, .preset = -1 };

static struct {
    bool built;
    lv_obj_t *dot, *name, *hint, *page;
    ui_pager_t pager;
    /* The page showing: a control (pages 1..3; page 1 with the IR buttons) or the microphone choice (page 4). */
    lv_obj_t *power, *phase;
    ui_value_ctrl_t *ctrl[NANO_CAB_PARAMS];
    lv_obj_t *mic_btn[MIC_BUTTONS], *pos_btn[NANO_CAB_POSITIONS], *mic_note;
} w;

/* ---- scales: the pedal's 0..1 against each control's raw value ---------------------- */

static int level_pos(int raw) { return (int)lroundf(nano_cab_normalized(NANO_CAB_LEVEL, (raw - IR_LEVEL_ZERO) / 10.0f) * 1000.0f); }
static int level_raw(int pos) { return (int)lroundf(nano_cab_value(NANO_CAB_LEVEL, pos / 1000.0f) * 10.0f) + IR_LEVEL_ZERO; }
static int hp_pos(int raw) { return (int)lroundf(nano_cab_normalized(NANO_CAB_HIGH_PASS, (float)raw) * 1000.0f); }
static int hp_raw(int pos) { return (int)lroundf(nano_cab_value(NANO_CAB_HIGH_PASS, pos / 1000.0f)); }
static int lp_pos(int raw) { return (int)lroundf(nano_cab_normalized(NANO_CAB_LOW_PASS, (float)raw) * 1000.0f); }
static int lp_raw(int pos) { return (int)lroundf(nano_cab_value(NANO_CAB_LOW_PASS, pos / 1000.0f)); }
static int same(int v) { return v; }
static int lp_readout(int raw) { return raw / 100; }
static void level_format(int raw, char *out, size_t cap) { ui_format_db_tenths(raw - IR_LEVEL_ZERO, out, cap); }
static void hz_format(int raw, char *out, size_t cap) { snprintf(out, cap, "%d Hz", raw); }

static void khz_format(int raw, char *out, size_t cap)
{
    int r = raw / 100;
    snprintf(out, cap, "%d.%d kHz", r / 10, r % 10);
}

static int raw_of(nano_cab_param_t p, float n)
{
    float v = nano_cab_value(p, n);
    return p == NANO_CAB_LEVEL ? (int)lroundf(v * 10.0f) + IR_LEVEL_ZERO : (int)lroundf(v);
}

static float normalized_of(nano_cab_param_t p, int raw)
{
    return nano_cab_normalized(p, p == NANO_CAB_LEVEL ? (raw - IR_LEVEL_ZERO) / 10.0f : (float)raw);
}

static const ui_value_ctrl_cfg_t CFG[NANO_CAB_PARAMS] = {
    [NANO_CAB_LEVEL] = {
        .raw_min = 0, .raw_max = IR_LEVEL_ZERO + 120,
        .pos_min = 0, .pos_max = 1000,
        .raw_to_pos = level_pos, .pos_to_raw = level_raw,
        .readout = same, .format = level_format,
        .fine = 1, .coarse = 10,
        .step_labels = { "-1 dB", "-0.1", "+0.1", "+1 dB" },
        .reset_raw = IR_LEVEL_ZERO,
        .turn = 6,
    },
    [NANO_CAB_HIGH_PASS] = {
        .raw_min = 20, .raw_max = 800,
        .pos_min = 0, .pos_max = 1000,
        .raw_to_pos = hp_pos, .pos_to_raw = hp_raw,
        .readout = same, .format = hz_format,
        .fine = 1, .coarse = 10,
        .step_labels = { "-10 Hz", "-1", "+1", "+10 Hz" },
        .reset_raw = -1,
        .turn = 6,
    },
    [NANO_CAB_LOW_PASS] = {
        .raw_min = 1000, .raw_max = 20000,
        .pos_min = 0, .pos_max = 1000,
        .raw_to_pos = lp_pos, .pos_to_raw = lp_raw,
        .readout = lp_readout, .format = khz_format,
        .fine = 1, .coarse = 10,
        .step_labels = { "-1 kHz", "-0.1", "+0.1", "+1 kHz" },
        .reset_raw = -1,
        .turn = 6,
    },
};
static const char *const CAPTIONS[NANO_CAB_PARAMS] = { "Level", "High pass", "Low pass" };

/* ---- refresh -------------------------------------------------------------------------- */

static void choice_style(lv_obj_t *b, bool selected, bool enabled)
{
    lv_obj_set_style_border_width(b, selected ? 2 : 0, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(selected ? C_PANEL_2 : C_PANEL), 0);
    lv_obj_set_style_opa(b, enabled ? LV_OPA_COVER : LV_OPA_40, 0);
    if (enabled) lv_obj_remove_state(b, LV_STATE_DISABLED);
    else lv_obj_add_state(b, LV_STATE_DISABLED);
}

/* Microphone page: the IR's microphones (as the pedal lists them) and the six positions. */
static void refresh_mics(void)
{
    if (!w.mic_note) return; /* another page shows */
    bool usable = s.on && s.known && s.set.factory && g_ui.cb.on_cab_mic;
    for (int i = 0; i < MIC_BUTTONS; i++) {
        lv_obj_t *b = w.mic_btn[i];
        bool shown = s.known && s.set.factory && i < s.set.mic_count;
        lv_obj_set_hidden(b, !shown);
        if (!shown) continue;
        lv_label_set_text(lv_obj_get_child(b, 0), s.set.mics[i]);
        choice_style(b, strcmp(s.set.mics[i], s.set.mic) == 0, usable);
    }
    for (int i = 0; i < NANO_CAB_POSITIONS; i++) {
        lv_obj_set_hidden(w.pos_btn[i], !(s.known && s.set.factory));
        choice_style(w.pos_btn[i], s.set.position == i, usable);
    }
    lv_label_set_text(w.mic_note, !s.known || s.set.factory ? "" : "Your own IR: no microphone choice");
}

/* Page 1's buttons: On / Off (green while on) and Phase (blue while inverted). */
static void refresh_buttons(void)
{
    if (w.power) {
        lv_obj_set_style_bg_color(w.power, lv_color_hex(s.on ? C_ON : C_PANEL), 0);
        lv_obj_t *l = lv_obj_get_child(w.power, 0);
        lv_label_set_text(l, s.on ? LV_SYMBOL_POWER " On" : LV_SYMBOL_POWER " Off");
        lv_obj_set_style_text_color(l, lv_color_hex(s.on ? C_FX_TEXT : C_MUTED), 0);
    }
    if (!w.phase) return; /* another page shows */
    bool usable = s.on && s.known;
    lv_obj_set_style_bg_color(w.phase, lv_color_hex(s.set.phase_inverted ? C_ACCENT : C_PANEL), 0);
    lv_obj_set_style_text_color(lv_obj_get_child(w.phase, 0), lv_color_hex(s.set.phase_inverted ? C_FX_TEXT : C_TEXT), 0);
    lv_obj_set_style_opa(w.phase, usable ? LV_OPA_COVER : LV_OPA_40, 0);
}

/* The IR the shown preset was saved with (the metadata's record), as opposed to one picked since. */
bool ir_is_saved(const char *name)
{
    return name[0] && g_ui.meta && s.preset >= 0 && s.preset < NANO_PRESET_COUNT && strcmp(name, g_ui.meta->presets[s.preset].ir_short_name) == 0;
}

const char *ir_tab_name(void) { return s.name; }

/* Name and why there are no values: the hint at the right of the name line, the name shortened to make room. */
static void refresh_name(const char *hint)
{
    ui_line_set(w.dot, w.name, s.name, s.on, "No IR");
    if (s.on && ir_is_saved(s.name)) lv_obj_set_style_text_color(w.name, lv_color_hex(C_SAVED), 0);
    lv_label_set_text(w.hint, hint);
    lv_point_t size = { 0 };
    if (hint[0]) lv_text_get_size(&size, hint, &lv_font_montserrat_14, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    lv_obj_set_width(w.name, NAME_W - (hint[0] ? size.x + 8 : 0));
}

/* Name, hint and the controls: values only while the IR is on and the pedal has answered. `fresh` = the pedal's
 * answer to a read after the last change here (shown even while a control holds its own). */
void ir_tab_refresh(bool fresh)
{
    if (!w.built) return;
    refresh_name(!s.on ? "IR is off" : !s.known ? (s.unreadable ? "No settings" : "Reading...") : "");
    for (int i = 0; i < NANO_CAB_PARAMS; i++) {
        int raw = s.on && s.known ? raw_of((nano_cab_param_t)i, s.set.values[i]) : -1;
        if (fresh) ui_value_ctrl_set(w.ctrl[i], raw, s.owner);
        else ui_value_ctrl_report(w.ctrl[i], raw, s.owner);
    }
    refresh_mics();
    refresh_buttons();
}

/* ---- events ------------------------------------------------------------------------------ */

static void on_changed(int raw, void *user)
{
    nano_cab_param_t p = (nano_cab_param_t)(intptr_t)user;
    s.set.values[p] = normalized_of(p, raw);
    if (g_ui.cb.on_cab_setting) g_ui.cb.on_cab_setting((uint8_t)p, s.set.values[p]);
}

static void on_mic(lv_event_t *e)
{
    lv_obj_t *b = lv_event_get_target_obj(e);
    if (!s.on || !s.known || !s.set.factory || !g_ui.cb.on_cab_mic) return;
    for (int i = 0; i < MIC_BUTTONS; i++) {
        if (b == w.mic_btn[i] && i < s.set.mic_count) snprintf(s.set.mic, sizeof(s.set.mic), "%s", s.set.mics[i]);
    }
    for (int i = 0; i < NANO_CAB_POSITIONS; i++) {
        if (b == w.pos_btn[i]) s.set.position = (uint8_t)i;
    }
    refresh_mics(); /* shown now; the app reads the IR again to confirm */
    g_ui.cb.on_cab_mic(s.set.position, s.set.mic);
}

static void on_phase(lv_event_t *e)
{
    (void)e;
    if (!s.on || !s.known || !g_ui.cb.on_cab_phase) return;
    s.set.phase_inverted = !s.set.phase_inverted;
    refresh_buttons(); /* shown now; the app reads the IR again to confirm */
    g_ui.cb.on_cab_phase(s.set.phase_inverted);
}

/* On / Off and < >: the app shows the new IR at once (a state from the cache), the pedal's next dump confirms it. */
static void on_power(lv_event_t *e)
{
    (void)e;
    if (g_ui.cb.on_cab_on) g_ui.cb.on_cab_on(!s.on);
}

static void on_open_list(lv_event_t *e) { (void)e; ui_go(NANO_VIEW_IR_LIST); }

static void on_step_ir(lv_event_t *e)
{
    if (g_ui.cb.on_cab_step) g_ui.cb.on_cab_step((int)(intptr_t)lv_event_get_user_data(e));
}

/* ---- pages ----------------------------------------------------------------------------------- */

static lv_obj_t *choice_button(lv_obj_t *p, int32_t x, int32_t y, int32_t width, const char *text, const lv_font_t *font)
{
    lv_obj_t *b = ui_button(p, x, y, width, 34, text, font, C_PANEL, C_TEXT, on_mic, NULL);
    lv_obj_set_style_radius(b, 8, 0);
    lv_obj_set_style_border_color(b, lv_color_hex(C_ACCENT), 0);
    return b;
}

static void build_mic_page(lv_obj_t *p)
{
    const int32_t width = SCREEN_W - SETTING_X - 8, gap = 6;
    lv_obj_t *cap = ui_label(p, &lv_font_montserrat_14, C_MUTED);
    lv_label_set_text(cap, "Microphone");
    const int32_t mw = (width - 2 * gap) / 3, mh = 34;
    for (int i = 0; i < MIC_BUTTONS; i++) {
        int row = i / 3, col = i % 3;
        lv_obj_t *b = choice_button(p, col * (mw + gap), 20 + row * (mh + 4), mw, "", &montserrat_medium_12);
        lv_obj_t *l = lv_obj_get_child(b, 0);
        lv_obj_set_width(l, mw - 6);
        lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_center(l);
        lv_obj_set_hidden(b, true);
        w.mic_btn[i] = b;
    }
    const int32_t py = 20 + 2 * (mh + 4) + 6;
    lv_obj_t *pc = ui_label(p, &lv_font_montserrat_14, C_MUTED);
    lv_label_set_text(pc, "Position");
    lv_obj_set_pos(pc, 0, py);
    const int32_t pw = (width - 5 * gap) / NANO_CAB_POSITIONS;
    for (int i = 0; i < NANO_CAB_POSITIONS; i++) {
        char t[4];
        snprintf(t, sizeof(t), "%d", i + 1);
        w.pos_btn[i] = choice_button(p, i * (pw + gap), py + 20, pw, t, &lv_font_montserrat_14);
    }
    w.mic_note = ui_label(p, &lv_font_montserrat_14, C_MUTED);
    lv_obj_set_pos(w.mic_note, 0, 24);
    lv_obj_set_width(w.mic_note, width);
}

/* Page 1's row above Level: [On / Off] [Ø]  ...  [<] [>]. */
static void build_ir_buttons(lv_obj_t *p)
{
    const int32_t right = SCREEN_W - SETTING_X - 8, wide = 70, narrow = 50, gap = 6;
    if (g_ui.cb.on_cab_on) {
        w.power = ui_button(p, 0, 0, wide, IR_BUTTON_H, "", &lv_font_montserrat_14, C_PANEL, C_TEXT, on_power, NULL);
        lv_obj_set_style_radius(w.power, 8, 0);
    }
    if (g_ui.cb.on_cab_phase) {
        w.phase = ui_button(p, wide + gap, 0, wide, IR_BUTTON_H, "\xC3\x98", &montserrat_medium_20_phase, C_PANEL, C_TEXT, on_phase, NULL); /* Ø */
        lv_obj_set_style_radius(w.phase, 8, 0);
    }
    if (g_ui.cb.on_cab_step) {
        lv_obj_t *prev = ui_button(p, right - 2 * narrow - gap, 0, narrow, IR_BUTTON_H, LV_SYMBOL_LEFT, &lv_font_montserrat_14, C_PANEL, C_TEXT, on_step_ir, (void *)(intptr_t)-1);
        lv_obj_t *next = ui_button(p, right - narrow, 0, narrow, IR_BUTTON_H, LV_SYMBOL_RIGHT, &lv_font_montserrat_14, C_PANEL, C_TEXT, on_step_ir, (void *)(intptr_t)1);
        lv_obj_set_style_radius(prev, 8, 0);
        lv_obj_set_style_radius(next, 8, 0);
    }
}

static void forget_page(void)
{
    w.power = w.phase = NULL;
    memset(w.ctrl, 0, sizeof(w.ctrl));
    memset(w.mic_btn, 0, sizeof(w.mic_btn));
    memset(w.pos_btn, 0, sizeof(w.pos_btn));
    w.mic_note = NULL;
}

/* Pages 1..3 Level (under the IR buttons, its caption beside the value), High pass, Low pass; 4 microphone and
 * position. */
static void show_page(int idx)
{
    lv_obj_clean(w.page);
    forget_page();
    if (idx < NANO_CAB_PARAMS) {
        const int32_t ctrl_y = idx == NANO_CAB_LEVEL ? IR_BUTTON_H + 4 : 26;
        if (idx == NANO_CAB_LEVEL) build_ir_buttons(w.page);
        lv_obj_t *cap = ui_label(w.page, &lv_font_montserrat_14, C_MUTED);
        lv_label_set_text(cap, CAPTIONS[idx]);
        if (idx == NANO_CAB_LEVEL) lv_obj_set_y(cap, ctrl_y + 8);
        ui_value_ctrl_cfg_t cfg = CFG[idx];
        cfg.on_change = g_ui.cb.on_cab_setting ? on_changed : NULL;
        cfg.user = (void *)(intptr_t)idx;
        w.ctrl[idx] = ui_value_ctrl_create(w.page, ctrl_y, &cfg);
    } else {
        build_mic_page(w.page);
    }
    ir_tab_refresh(true); /* the new page's controls start from the pedal's values */
}

void ir_tab_build(lv_obj_t *content)
{
    /* Name with its on / off dot (one line); why there are no values at the right of it. */
    w.built = true;
    w.dot = ui_dot(content, 14, SOURCE_NAME_Y + 6, 12);
    w.name = ui_line_label(content, NAME_X, SOURCE_NAME_Y, NAME_W, &lv_font_montserrat_20, C_TEXT);
    /* Hold the name: every IR on the pedal (a tap does nothing). */
    lv_obj_set_clickable(w.name, true);
    lv_obj_set_ext_click_area(w.name, 6);
    lv_obj_add_event_cb(w.name, on_open_list, LV_EVENT_LONG_PRESSED, NULL);
    w.hint = ui_label(content, &lv_font_montserrat_14, C_WARN);
    lv_obj_set_width(w.hint, NAME_W);
    lv_obj_set_pos(w.hint, NAME_X, SOURCE_NAME_Y + 3);
    lv_obj_set_style_text_align(w.hint, LV_TEXT_ALIGN_RIGHT, 0);

    /* A page per setting, the pager on the left (content coordinates). */
    const int32_t top = 68 - SOURCE_TOP, h = SCREEN_H - SOURCE_TOP - top - 4;
    w.page = ui_box(content, SETTING_X, top, SCREEN_W - SETTING_X, h, C_BG);
    ui_pager_create(&w.pager, content, top, h, "Page");
    w.pager.on_show = show_page;
    ui_pager_set_count(&w.pager, PAGES, 0);
}

void ir_tab_destroy(void)
{
    w.built = false;
    forget_page();
}

/* ---- the app's side ------------------------------------------------------------------------- */

void ir_tab_from_state(const nano_state_t *st)
{
    snprintf(s.name, sizeof(s.name), "%s", st->ir_short_name);
    s.on = st->cab_on;
    s.preset = st->active_preset;
    if (s.owner != st->active_preset) s.known = s.unreadable = false; /* the app reads the new preset's settings */
    ir_tab_refresh(false);
    ir_list_refresh();
}

void nano_ui_set_ir_settings(const nano_cab_settings_t *settings, int preset, bool fresh)
{
    s.owner = preset;
    s.known = settings != NULL;
    s.unreadable = settings == NULL;
    if (settings) s.set = *settings;
    ir_tab_refresh(fresh);
}

void nano_ui_ir_page(int index)
{
    if (w.built) ui_pager_show(&w.pager, index);
}
