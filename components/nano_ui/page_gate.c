/*
 * Gate page (hold GATE on the gig view): On / Off and the threshold as a ui_value_ctrl. The threshold runs on the
 * pedal's raw 0..255 (nano_build_gate_threshold, state field 53) and shows as Cortex Cloud's 0..100 %. Built on open.
 */
#include <stdio.h>

#include "nano_models.h"
#include "ui_internal.h"
#include "ui_value_ctrl.h"
#include "ui_widgets.h"

/* The gate as the last state showed it. */
static struct {
    bool on;
    int threshold;   /* raw 0..255, -1 = unknown */
    int preset;
} s = { .threshold = -1, .preset = -1 };

static struct {
    lv_obj_t *power;
    ui_value_ctrl_t *threshold; /* freed with the page */
} w;

static int same(int v) { return v; }
/* Tenths of a percent, as Cortex Cloud shows it (raw 50 = 19.6 %). */
static int tenths(int raw) { return (raw * 1000 + 127) / 255; }
static void percent_format(int raw, char *out, size_t cap) { snprintf(out, cap, "%d.%d %%", tenths(raw) / 10, tenths(raw) % 10); }

static void refresh(void)
{
    if (!w.power) return;
    uint32_t color = nano_category_color(NANO_CAT_UTILITY);
    lv_obj_set_style_bg_color(w.power, lv_color_hex(s.on ? color : C_PANEL), 0);
    lv_obj_t *l = lv_obj_get_child(w.power, 0);
    lv_label_set_text(l, s.on ? LV_SYMBOL_POWER " On" : LV_SYMBOL_POWER " Off");
    lv_obj_set_style_text_color(l, lv_color_hex(s.on ? C_FX_TEXT : C_MUTED), 0);
    ui_value_ctrl_report(w.threshold, s.threshold, s.preset);
}

static void on_power(lv_event_t *e)
{
    (void)e;
    if (g_ui.cb.on_toggle_gate) g_ui.cb.on_toggle_gate(s.on);
}

static void on_threshold(int raw, void *user)
{
    (void)user;
    s.threshold = raw;
    if (g_ui.cb.on_gate_threshold) g_ui.cb.on_gate_threshold((uint8_t)raw);
}

static void on_close(lv_event_t *e) { (void)e; ui_go_base(); }

static lv_obj_t *build(lv_obj_t *scr)
{
    ui_overlay_t o = ui_overlay(scr, "Gate", on_close, on_close);
    w.power = ui_button(o.root, 16, TOP_H + 10, SCREEN_W - 32, 44, "", &lv_font_montserrat_20, C_PANEL, C_TEXT, on_power, NULL);
    lv_obj_set_style_radius(w.power, 10, 0);
    lv_obj_t *cap = ui_label(o.root, &lv_font_montserrat_14, C_MUTED);
    lv_label_set_text(cap, "Threshold");
    lv_obj_set_pos(cap, 16, TOP_H + 64);
    const ui_value_ctrl_cfg_t cfg = {
        .raw_min = 0, .raw_max = 255,
        .pos_min = 0, .pos_max = 255,
        .raw_to_pos = same, .pos_to_raw = same,
        .readout = tenths, .format = percent_format,
        /* Buttons fine-tune, the slider covers the range: one raw step (1/255 = ~0.4 %, the finest the write can
         * say) and 1 % (the closest step to it). */
        .fine = 1, .coarse = 10,
        .step_labels = { "-1 %", "-0.4", "+0.4", "+1 %" },
        .reset_raw = -1,
        .on_change = g_ui.cb.on_gate_threshold ? on_threshold : NULL,
    };
    w.threshold = ui_value_ctrl_create(o.root, SCREEN_H - UI_VALUE_CTRL_HEIGHT - 6, &cfg);
    refresh();
    return o.root;
}

static void destroy(void)
{
    w.power = NULL;
    w.threshold = NULL;
}

ui_page_t page_gate = { .build = build, .destroy = destroy, .needs_link = true };

/* While the page is open the control decides whether a report shows (it ignores ones requested before its change). */
void gate_from_state(const nano_state_t *st)
{
    s.on = st->gate_on;
    s.threshold = st->gate_threshold;
    s.preset = st->active_preset;
    refresh();
    if (w.threshold) s.threshold = ui_value_ctrl_value(w.threshold);
}
