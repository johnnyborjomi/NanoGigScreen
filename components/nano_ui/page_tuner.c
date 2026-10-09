/*
 * Tuner: the note, a cents bar (-50..+50 around a centre mark), the cents and a verdict; the mute state in the
 * title bar (tap: the app re-sends tuner-on with the other flag, as Cortex Cloud does). Opening it turns the
 * pedal's tuner on, closing turns it off (unless the pedal did either itself). Built on open.
 */
#include <stdio.h>

#include "ui_internal.h"
#include "ui_widgets.h"

#define BAR_Y 108
#define C_MUTED_BG 0x3A2A10

static bool s_muted;
static struct {
    lv_obj_t *note, *cents, *bar, *verdict, *mute;
} w;

static void on_back(lv_event_t *e) { (void)e; ui_go(NANO_VIEW_MENU); }
static void on_close(lv_event_t *e) { (void)e; ui_go_base(); }

static void on_mute_clicked(lv_event_t *e)
{
    (void)e;
    if (g_ui.cb.on_tuner_mute) g_ui.cb.on_tuner_mute(!s_muted);
}

static lv_obj_t *centred_label(lv_obj_t *parent, int32_t y, const lv_font_t *font, const char *text)
{
    lv_obj_t *l = ui_label(parent, font, C_MUTED);
    lv_obj_set_width(l, SCREEN_W);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(l, 0, y);
    lv_label_set_text(l, text);
    return l;
}

static lv_obj_t *build(lv_obj_t *scr)
{
    ui_overlay_t o = ui_overlay(scr, "Tuner", on_back, on_close);
    lv_obj_t *root = o.root;
    w.note = centred_label(root, 40, &lv_font_montserrat_40, "-");
    ui_box(root, 20, BAR_Y + 2, SCREEN_W - 40, 14, C_PANEL_2);
    lv_obj_set_style_radius(lv_obj_get_child(root, -1), 7, 0);
    ui_box(root, SCREEN_W / 2 - 1, BAR_Y - 8, 2, 34, C_MUTED); /* centre mark */
    w.bar = ui_box(root, SCREEN_W / 2 - 6, BAR_Y, 12, 18, C_WARN);
    lv_obj_set_style_radius(w.bar, 6, 0);
    w.cents = centred_label(root, 140, &lv_font_montserrat_20, "");
    w.verdict = centred_label(root, 166, &lv_font_montserrat_14, "Play a note");
    w.mute = ui_button(root, SCREEN_W - 44 - 96, 4, 92, 22, "SOUND ON", &montserrat_medium_10, C_PANEL, C_MUTED, on_mute_clicked, NULL);
    lv_obj_set_style_radius(w.mute, 7, 0);
    lv_obj_set_ext_click_area(w.mute, 8);
    ui_button(root, 12, SCREEN_H - 50, SCREEN_W - 24, 40, "Done", &lv_font_montserrat_20, C_PANEL, C_TEXT, on_close, NULL);
    nano_ui_set_tuner_mute(s_muted);
    return root;
}

static void destroy(void) { w.note = NULL; }

static void enter(nano_view_t view, nano_view_t from, bool notify)
{
    (void)view, (void)from;
    nano_ui_set_tuner(NULL, 0, false);
    if (notify && g_ui.cb.on_tuner) g_ui.cb.on_tuner(true);
}

static void leave(nano_view_t to, bool notify)
{
    (void)to;
    if (notify && g_ui.cb.on_tuner) g_ui.cb.on_tuner(false);
}

ui_page_t page_tuner = { .build = build, .destroy = destroy, .enter = enter, .leave = leave, .needs_link = true };

void nano_ui_set_tuner(const char *note, float cents, bool in_tune)
{
    if (!w.note) return;
    if (!note || !note[0]) {
        lv_label_set_text(w.note, "-");
        lv_obj_set_style_text_color(w.note, lv_color_hex(C_DIM), 0);
        lv_label_set_text(w.cents, "");
        lv_label_set_text(w.verdict, "Play a note");
        lv_obj_set_hidden(w.bar, true);
        return;
    }
    lv_label_set_text(w.note, note);
    uint32_t c = in_tune ? C_ON : (cents < -10 || cents > 10) ? C_ERROR : C_WARN;
    lv_obj_set_style_text_color(w.note, lv_color_hex(c), 0);
    lv_obj_set_style_bg_color(w.bar, lv_color_hex(c), 0);
    lv_obj_set_hidden(w.bar, false);
    float clamped = cents < -50 ? -50 : cents > 50 ? 50 : cents;
    int32_t px = (int32_t)(clamped * (SCREEN_W / 2 - 26) / 50.0f);
    lv_obj_set_pos(w.bar, SCREEN_W / 2 - 6 + px, BAR_Y);
    char t[24];
    snprintf(t, sizeof(t), "%+d ct", (int)(cents < 0 ? cents - 0.5f : cents + 0.5f));
    lv_label_set_text(w.cents, t);
    lv_label_set_text(w.verdict, in_tune ? "In tune" : cents < 0 ? "Flat" : "Sharp");
}

void nano_ui_set_tuner_mute(bool muted)
{
    s_muted = muted;
    if (!w.note) return;
    lv_obj_t *l = lv_obj_get_child(w.mute, 0);
    lv_label_set_text(l, muted ? "MUTED" : "SOUND ON");
    lv_obj_set_style_text_color(l, lv_color_hex(muted ? C_WARN : C_MUTED), 0);
    lv_obj_set_style_bg_color(w.mute, lv_color_hex(muted ? C_MUTED_BG : C_PANEL), 0);
    lv_obj_center(l);
}
