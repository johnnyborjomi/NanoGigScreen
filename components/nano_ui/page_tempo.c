/*
 * Tempo: the BPM big, - / + that step on the press itself and repeat while held. Opening it puts the pedal in its
 * tap tempo mode, closing takes it out (unless the pedal did either itself). Built on open.
 */
#include <stdio.h>

#include "ui_internal.h"
#include "ui_widgets.h"

static struct {
    lv_obj_t *big, *hint;
} w;

static void on_back(lv_event_t *e) { (void)e; ui_go(NANO_VIEW_MENU); }
static void on_close(lv_event_t *e) { (void)e; ui_go_base(); }

static void on_step(lv_event_t *e)
{
    int delta = (int)(intptr_t)lv_event_get_user_data(e);
    if (g_ui.cb.on_tempo_delta) g_ui.cb.on_tempo_delta(delta);
}

static lv_obj_t *centred_label(lv_obj_t *parent, int32_t y, const lv_font_t *font, uint32_t color, const char *text)
{
    lv_obj_t *l = ui_label(parent, font, color);
    lv_obj_set_width(l, SCREEN_W);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(l, 0, y);
    lv_label_set_text(l, text);
    return l;
}

/* Steps on the press (not the release) and repeats while held. */
static void step_button(lv_obj_t *parent, int32_t x, const char *symbol, int delta)
{
    lv_obj_t *b = ui_button(parent, x, 134, 140, 48, symbol, &lv_font_montserrat_20, C_PANEL, C_TEXT, NULL, NULL);
    lv_obj_add_event_cb(b, on_step, LV_EVENT_PRESSED, (void *)(intptr_t)delta);
    lv_obj_add_event_cb(b, on_step, LV_EVENT_LONG_PRESSED_REPEAT, (void *)(intptr_t)delta);
}

static lv_obj_t *build(lv_obj_t *scr)
{
    ui_overlay_t o = ui_overlay(scr, "Tempo", on_back, on_close);
    w.big = centred_label(o.root, 44, &lv_font_montserrat_40, C_ON, "-");
    centred_label(o.root, 92, &lv_font_montserrat_14, C_MUTED, "BPM");
    w.hint = centred_label(o.root, 112, &lv_font_montserrat_12, C_MUTED, "");
    step_button(o.root, 12, LV_SYMBOL_MINUS, -1);
    step_button(o.root, SCREEN_W - 12 - 140, LV_SYMBOL_PLUS, 1);
    ui_button(o.root, 12, SCREEN_H - 50, SCREEN_W - 24, 40, "Done", &lv_font_montserrat_20, C_PANEL, C_TEXT, on_close, NULL);
    tempo_page_refresh();
    return o.root;
}

static void destroy(void) { w.big = NULL; }

static void enter(nano_view_t view, nano_view_t from, bool notify)
{
    (void)view, (void)from;
    if (notify && g_ui.cb.on_tempo_view) g_ui.cb.on_tempo_view(true);
}

static void leave(nano_view_t to, bool notify)
{
    (void)to;
    if (notify && g_ui.cb.on_tempo_view) g_ui.cb.on_tempo_view(false);
}

ui_page_t page_tempo = { .build = build, .destroy = destroy, .enter = enter, .leave = leave, .needs_link = true };

void tempo_page_refresh(void)
{
    if (!w.big) return;
    if (g_ui.tempo_bpm <= 0) {
        lv_label_set_text(w.big, "-");
        return;
    }
    char t[8];
    snprintf(t, sizeof(t), "%d", (int)(g_ui.tempo_bpm + 0.5f));
    lv_label_set_text(w.big, t);
    lv_obj_set_style_text_color(w.big, lv_color_hex(g_ui.tempo_tapping ? C_WARN : C_ON), 0);
    lv_label_set_text(w.hint, g_ui.tempo_tapping ? "Tap tempo on the pedal" : "");
}

void nano_ui_set_tempo(float bpm, bool tapping)
{
    g_ui.tempo_bpm = bpm;
    g_ui.tempo_tapping = tapping;
    main_refresh_tempo();
    tempo_page_refresh();
}
