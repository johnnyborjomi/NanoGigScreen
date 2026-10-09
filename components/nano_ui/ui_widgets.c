#include "ui_widgets.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ui_internal.h"

/* ---- overlay page --------------------------------------------------------------- */

ui_overlay_t ui_overlay(lv_obj_t *scr, const char *title, lv_event_cb_t back_cb, lv_event_cb_t close_cb)
{
    ui_overlay_t o = { 0 };
    o.root = ui_box(scr, 0, 0, SCREEN_W, SCREEN_H, C_BG);
    if (back_cb) {
        o.back = ui_button(o.root, 0, 0, 44, TOP_H + 4, LV_SYMBOL_LEFT, &lv_font_montserrat_14, C_BG, C_MUTED, back_cb, NULL);
        lv_obj_set_ext_click_area(o.back, 6);
    }
    o.title = ui_label(o.root, &lv_font_montserrat_14, C_MUTED);
    lv_label_set_text(o.title, title);
    lv_obj_set_pos(o.title, back_cb ? 40 : 12, 8);
    o.close = ui_button(o.root, SCREEN_W - 44, 0, 44, TOP_H + 4, LV_SYMBOL_CLOSE, &lv_font_montserrat_14, C_BG, C_MUTED, close_cb, NULL);
    lv_obj_set_ext_click_area(o.close, 6);
    lv_obj_set_hidden(o.root, true);
    return o;
}

void ui_overlay_set_back(const ui_overlay_t *o, bool shown)
{
    if (o->back) lv_obj_set_hidden(o->back, !shown);
    lv_obj_set_x(o->title, shown ? 40 : 12);
}

/* ---- pager ------------------------------------------------------------------------ */

void ui_pager_show(ui_pager_t *p, int idx)
{
    if (idx < 0 || idx >= p->count) return;
    p->current = idx;
    for (int i = 0; i < p->count && i < UI_PAGER_MAX_PAGES; i++) if (p->pages[i]) lv_obj_set_hidden(p->pages[i], i != idx);
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
    ui_pager_t *p = lv_event_get_user_data(e);
    int delta = lv_event_get_target_obj(e) == p->up ? -1 : 1;
    int idx = p->current + delta;
    if (p->wrap && p->count > 0) idx = (idx + p->count) % p->count;
    ui_pager_show(p, idx);
}

void ui_pager_create(ui_pager_t *p, lv_obj_t *parent, int32_t y, int32_t h, const char *unit)
{
    memset(p, 0, sizeof(*p));
    p->unit = unit;
    p->mid_y = y + h / 2;
    p->up = ui_button(parent, EDGE_X, y, PAGER_W, UI_PAGER_BTN_H, LV_SYMBOL_UP, &lv_font_montserrat_14, C_PANEL, C_MUTED, on_pager_step, p);
    p->down = ui_button(parent, EDGE_X, y + h - UI_PAGER_BTN_H, PAGER_W, UI_PAGER_BTN_H, LV_SYMBOL_DOWN, &lv_font_montserrat_14, C_PANEL, C_MUTED, on_pager_step, p);
    p->label = ui_label(parent, &montserrat_medium_12, C_MUTED);
    lv_label_set_text(p->label, "");
    lv_obj_set_x(p->label, EDGE_X);
    lv_obj_set_width(p->label, PAGER_W);
    lv_obj_set_style_text_align(p->label, LV_TEXT_ALIGN_CENTER, 0);
}

lv_obj_t *ui_pager_add_page(ui_pager_t *p, lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h)
{
    if (p->count >= UI_PAGER_MAX_PAGES) return NULL;
    lv_obj_t *page = ui_box(parent, x, y, w, h, C_BG);
    p->pages[p->count++] = page;
    ui_pager_show(p, p->current);
    return page;
}

void ui_pager_set_count(ui_pager_t *p, int count, int current)
{
    p->count = count;
    ui_pager_show(p, current < count ? current : count - 1);
}

/* ---- settings rows -------------------------------------------------------------------- */

lv_obj_t *ui_setting_caption(lv_obj_t *parent, int32_t y, const char *text)
{
    lv_obj_t *l = ui_label(parent, &lv_font_montserrat_14, C_TEXT);
    lv_label_set_text(l, text);
    lv_obj_set_pos(l, 0, y + (SETTING_ROW_H - lv_font_get_line_height(&lv_font_montserrat_14)) / 2);
    return l;
}

lv_obj_t *ui_setting_stepper(lv_obj_t *page, int32_t y, lv_event_cb_t cb)
{
    ui_button(page, SETTING_RIGHT - 44, y, 44, SETTING_ROW_H, LV_SYMBOL_PLUS, &lv_font_montserrat_14, C_PANEL, C_TEXT, cb, (void *)(intptr_t)1);
    lv_obj_t *value = ui_label(page, &lv_font_montserrat_20, C_TEXT);
    lv_obj_set_pos(value, SETTING_RIGHT - 44 - 40, y + (SETTING_ROW_H - lv_font_get_line_height(&lv_font_montserrat_20)) / 2);
    lv_obj_set_width(value, 40);
    lv_obj_set_style_text_align(value, LV_TEXT_ALIGN_CENTER, 0);
    ui_button(page, SETTING_RIGHT - 44 - 40 - 44, y, 44, SETTING_ROW_H, LV_SYMBOL_MINUS, &lv_font_montserrat_14, C_PANEL, C_TEXT, cb, (void *)(intptr_t)-1);
    return value;
}

ui_switch_t ui_setting_switch(lv_obj_t *parent, int32_t y, lv_event_cb_t cb)
{
    ui_switch_t s;
    s.pill = ui_box(parent, SETTING_RIGHT - 52, y + (SETTING_ROW_H - 28) / 2, 52, 28, C_OFF);
    lv_obj_set_style_radius(s.pill, 14, 0);
    lv_obj_set_clickable(s.pill, true);
    lv_obj_set_ext_click_area(s.pill, 10);
    lv_obj_add_event_cb(s.pill, cb, LV_EVENT_CLICKED, NULL);
    s.knob = ui_box(s.pill, 0, 0, 22, 22, C_TEXT);
    lv_obj_set_style_radius(s.knob, LV_RADIUS_CIRCLE, 0);
    return s;
}

void ui_switch_set(const ui_switch_t *s, bool on, uint32_t on_color)
{
    lv_obj_set_style_bg_color(s->pill, lv_color_hex(on ? on_color : C_OFF), 0);
    lv_obj_align(s->knob, on ? LV_ALIGN_RIGHT_MID : LV_ALIGN_LEFT_MID, on ? -3 : 3, 0);
}

ui_segments_t ui_setting_segments(lv_obj_t *page, int32_t y, const char *const *labels, int count, int32_t w, lv_event_cb_t cb)
{
    const int32_t gap = 4;
    ui_segments_t s = { .count = count };
    for (int i = 0; i < count && i < UI_SEGMENTS_MAX; i++) {
        int32_t x = SETTING_RIGHT - (count - i) * w - (count - 1 - i) * gap;
        s.btn[i] = ui_button(page, x, y, w, SETTING_ROW_H, labels[i], &lv_font_montserrat_14, C_PANEL, C_TEXT, cb, (void *)(uintptr_t)i);
        lv_obj_set_style_radius(s.btn[i], 8, 0);
    }
    return s;
}

void ui_segments_select(const ui_segments_t *s, int selected)
{
    for (int i = 0; i < s->count; i++) {
        bool sel = i == selected;
        lv_obj_set_style_bg_color(s->btn[i], lv_color_hex(sel ? C_ACCENT : C_PANEL), 0);
        lv_obj_set_style_text_color(lv_obj_get_child(s->btn[i], 0), lv_color_hex(sel ? C_FX_TEXT : C_TEXT), 0);
    }
}

/* ---- status line ----------------------------------------------------------------------- */

lv_obj_t *ui_line_label(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, const lv_font_t *font, uint32_t color)
{
    lv_obj_t *l = ui_label(parent, font, color);
    lv_obj_set_pos(l, x, y);
    lv_obj_set_size(l, w, lv_font_get_line_height(font)); /* one line: LONG_DOT needs a fixed height */
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    return l;
}

void ui_line_set(lv_obj_t *dot, lv_obj_t *label, const char *name, bool on, const char *empty)
{
    lv_label_set_text(label, name && name[0] ? name : empty);
    lv_obj_set_style_bg_color(dot, lv_color_hex(on ? C_ON : C_DIM), 0);
    lv_obj_set_style_text_color(label, lv_color_hex(on ? C_TEXT : C_OFF_TEXT), 0);
}

void ui_format_db_tenths(int t, char *out, size_t cap)
{
    snprintf(out, cap, "%c%d.%d dB", t < 0 ? '-' : '+', abs(t) / 10, abs(t) % 10);
}
