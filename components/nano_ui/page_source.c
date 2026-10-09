/*
 * Capture / IR page: one page with a tab per source in the header (a view each, NANO_VIEW_CAPTURE / _IR). Only the
 * tab showing exists: both at once ran the heap out (2026-10-09, a crash drawing the pager label).
 *
 * Capture tab: name and on / off dot, then the volume as a ui_value_ctrl in Cortex Cloud's dB scale and readout
 * (-24..+12 dB, tenths cut toward zero; nano_capture_volume_*): the slider runs in tenths of a dB, steps of 0.1 and
 * 1 dB, double tap on the value for 0.0 dB (raw 128). The IR tab is ir_tab.c.
 */
#include <math.h>
#include <stdio.h>

#include "ir_tab.h"
#include "ui_internal.h"
#include "ui_value_ctrl.h"
#include "ui_widgets.h"

/* The capture as the last state showed it. */
static struct {
    char name[NANO_NAME_CAP];
    bool on;
    int volume;   /* raw 0..255, -1 = unknown */
    int preset;   /* preset the capture belongs to */
} s = { .volume = -1, .preset = -1 };

static struct {
    lv_obj_t *tabs[2], *content;
    int tab;      /* built: 0 = capture, 1 = IR, -1 = none */
    lv_obj_t *cap_dot, *cap_name;
    ui_value_ctrl_t *cap_volume; /* freed with the content */
} w = { .tab = -1 };

static void on_close(lv_event_t *e) { (void)e; ui_go_base(); }
static void on_tab(lv_event_t *e) { ui_go(lv_event_get_target_obj(e) == w.tabs[1] ? NANO_VIEW_IR : NANO_VIEW_CAPTURE); }

/* ---- capture tab ------------------------------------------------------------------- */

static int cap_vol_pos(int raw) { return (int)lroundf(nano_capture_volume_db((uint8_t)raw) * 10.0f); }
static int cap_vol_raw(int pos) { return nano_capture_volume_raw(pos / 10.0f); }
static int cap_vol_readout(int raw) { return nano_capture_volume_tenths((uint8_t)raw); }
static void cap_vol_format(int raw, char *out, size_t cap) { ui_format_db_tenths(nano_capture_volume_tenths((uint8_t)raw), out, cap); }

static void cap_vol_changed(int raw, void *user)
{
    (void)user;
    s.volume = raw;
    if (g_ui.cb.on_capture_volume) g_ui.cb.on_capture_volume((uint8_t)raw);
}

void capture_tab_refresh(void)
{
    if (w.tab != 0) return;
    ui_line_set(w.cap_dot, w.cap_name, s.name, s.on, "No capture");
}

static void build_capture_tab(lv_obj_t *page)
{
    /* Name with its on / off dot (up to two lines). */
    w.cap_dot = ui_dot(page, 14, SOURCE_NAME_Y + 6, 12);
    w.cap_name = ui_label(page, &lv_font_montserrat_20, C_TEXT);
    lv_obj_set_pos(w.cap_name, 34, SOURCE_NAME_Y);
    lv_obj_set_width(w.cap_name, SCREEN_W - 34 - 12);
    lv_label_set_long_mode(w.cap_name, LV_LABEL_LONG_WRAP);

    static const ui_value_ctrl_cfg_t VOLUME = {
        .raw_min = 0, .raw_max = 255,
        .pos_min = -240, .pos_max = 120,
        .raw_to_pos = cap_vol_pos, .pos_to_raw = cap_vol_raw,
        .readout = cap_vol_readout, .format = cap_vol_format,
        .fine = 1, .coarse = 10,
        .step_labels = { "-1 dB", "-0.1", "+0.1", "+1 dB" },
        .reset_raw = 128,
        .on_change = cap_vol_changed,
    };
    const int32_t ctrl_y = 92 - SOURCE_TOP;
    ui_value_ctrl_cfg_t cfg = VOLUME;
    if (!g_ui.cb.on_capture_volume) cfg.on_change = NULL; /* read-only */
    w.cap_volume = ui_value_ctrl_create(page, ctrl_y, &cfg);
    ui_value_ctrl_report(w.cap_volume, s.volume, s.preset);

    if (!g_ui.cb.on_capture_volume) {
        lv_obj_t *hint = ui_label(page, &lv_font_montserrat_12, C_MUTED);
        lv_obj_set_width(hint, SCREEN_W);
        lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(hint, 0, ctrl_y + UI_VALUE_CTRL_HEIGHT + 2);
        lv_label_set_text(hint, "Read-only for now");
    }
}

/* ---- page ------------------------------------------------------------------------- */

/* The tap comes from the header, so the old content can go at once. */
static void select_tab(int tab)
{
    for (int i = 0; i < 2; i++) {
        bool on = i == tab;
        lv_obj_set_style_bg_opa(w.tabs[i], on ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(w.tabs[i], on ? 2 : 0, 0);
        lv_obj_set_style_text_color(lv_obj_get_child(w.tabs[i], 0), lv_color_hex(on ? C_TEXT : C_MUTED), 0);
    }
    if (w.tab == tab) return;
    lv_obj_clean(w.content);
    w.cap_volume = NULL; /* freed with the content */
    ir_tab_destroy();
    w.tab = tab;
    if (tab == 0) build_capture_tab(w.content);
    else ir_tab_build(w.content);
}

static lv_obj_t *build(lv_obj_t *scr)
{
    ui_overlay_t o = ui_overlay(scr, "", on_close, on_close);
    /* Tabs between "<" and "x". */
    static const char *const NAMES[2] = { "Capture", "IR" };
    const int32_t x0 = 48, tab_w = (SCREEN_W - 2 * x0 - 6) / 2;
    for (int i = 0; i < 2; i++) {
        lv_obj_t *t = ui_button(o.root, x0 + i * (tab_w + 6), 1, tab_w, TOP_H + 2, NAMES[i], &lv_font_montserrat_14, C_PANEL_2, C_TEXT, on_tab, NULL);
        lv_obj_set_style_radius(t, 6, 0);
        lv_obj_set_style_border_side(t, LV_BORDER_SIDE_BOTTOM, 0);
        lv_obj_set_style_border_color(t, lv_color_hex(C_ACCENT), 0);
        lv_obj_set_ext_click_area(t, 4);
        w.tabs[i] = t;
    }
    /* Below the header, so the header stays tappable. */
    w.content = ui_box(o.root, 0, SOURCE_TOP, SCREEN_W, SCREEN_H - SOURCE_TOP, C_BG);
    w.tab = -1;
    return o.root;
}

static void destroy(void)
{
    w.tab = -1;
    w.cap_volume = NULL;
    ir_tab_destroy();
}

static void enter(nano_view_t view, nano_view_t from, bool notify)
{
    (void)notify;
    select_tab(view == NANO_VIEW_IR);
    capture_tab_refresh();
    ir_tab_refresh(false);
    /* The app reads the IR settings while their tab shows (after the tab exists: the answer lands in it). */
    if ((from == NANO_VIEW_IR) != (view == NANO_VIEW_IR) && g_ui.cb.on_ir_view) g_ui.cb.on_ir_view(view == NANO_VIEW_IR);
}

static void leave(nano_view_t to, bool notify)
{
    (void)to, (void)notify;
    if (g_ui.view == NANO_VIEW_IR && g_ui.cb.on_ir_view) g_ui.cb.on_ir_view(false);
}

ui_page_t page_source = { .build = build, .destroy = destroy, .enter = enter, .leave = leave, .needs_link = true };

bool source_tab_is(int tab) { return w.tab == tab; }

/* A state's capture and IR. While the page is open the volume control decides whether the volume shows (it ignores
 * reports that were requested before its latest change). */
void source_from_state(const nano_state_t *st)
{
    snprintf(s.name, sizeof(s.name), "%s", st->capture_name);
    s.on = st->capture_on;
    s.volume = st->capture_volume;
    s.preset = st->active_preset;
    if (w.cap_volume) {
        ui_value_ctrl_report(w.cap_volume, s.volume, s.preset);
        s.volume = ui_value_ctrl_value(w.cap_volume);
    }
    capture_tab_refresh();
    ir_tab_from_state(st);
}
