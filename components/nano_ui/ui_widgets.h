/*
 * Composite widgets the pages share (private to nano_ui): the overlay page with its title bar, the pager column,
 * settings rows (caption, stepper, switch, segmented choice), a status line (dot + one-line name).
 */
#ifndef UI_WIDGETS_H
#define UI_WIDGETS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "lvgl.h"

/* ---- overlay page --------------------------------------------------------------- */

typedef struct {
    lv_obj_t *root, *back, *title, *close; /* back NULL without a back button */
} ui_overlay_t;

/* Full-screen page with a title bar: "<" (back_cb; NULL = none), the title, "x" (close_cb). Hidden. */
ui_overlay_t ui_overlay(lv_obj_t *scr, const char *title, lv_event_cb_t back_cb, lv_event_cb_t close_cb);
/* Show or hide "<"; the title moves next to it or to the edge. */
void ui_overlay_set_back(const ui_overlay_t *o, bool shown);

/* Two tabs in an overlay's title bar, between "<" and "x" (Capture / IR, User / Factory); `cb` gets the tab's index
 * as user data. */
void ui_header_tabs(lv_obj_t *root, const char *const names[2], lv_event_cb_t cb, lv_obj_t *out[2]);
void ui_header_tabs_select(lv_obj_t *const tabs[2], int selected);

/* ---- pager: a left column with up / down buttons and "Page" over "n/m" between them ----
 * Pages are either child boxes shown one at a time (ui_pager_add_page) or virtual: a count and an on_show
 * callback that refills one box (ui_pager_set_count: the presets list, the IR tab). */

#define UI_PAGER_BTN_H 40
#define UI_PAGER_MAX_PAGES 4

typedef struct {
    lv_obj_t *up, *down, *label;
    lv_obj_t *pages[UI_PAGER_MAX_PAGES];
    int count, current;
    int32_t mid_y;
    const char *unit;          /* "Page", "Bank" */
    void (*on_show)(int idx);  /* virtual pages */
    bool wrap;                 /* past the last page lands on the first (and back) */
} ui_pager_t;

/* Column at the left edge from y to y + h (the pager must outlive its widgets: a page's static). */
void ui_pager_create(ui_pager_t *p, lv_obj_t *parent, int32_t y, int32_t h, const char *unit);
lv_obj_t *ui_pager_add_page(ui_pager_t *p, lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h);
void ui_pager_set_count(ui_pager_t *p, int count, int current);
void ui_pager_show(ui_pager_t *p, int idx);

/* ---- settings rows: caption at the left, the control against SETTING_RIGHT (page coordinates) ---- */

lv_obj_t *ui_setting_caption(lv_obj_t *parent, int32_t y, const char *text);
/* [-] value [+]; `cb` gets user data -1 / +1. Returns the value label. */
lv_obj_t *ui_setting_stepper(lv_obj_t *page, int32_t y, lv_event_cb_t cb);

/* iPhone-style switch: a 52 x 28 pill with a round knob; `on_color` fills it while on. */
typedef struct {
    lv_obj_t *pill, *knob;
} ui_switch_t;
ui_switch_t ui_setting_switch(lv_obj_t *parent, int32_t y, lv_event_cb_t cb);
void ui_switch_set(const ui_switch_t *s, bool on, uint32_t on_color);

/* A row of choice buttons ending at SETTING_RIGHT; `cb` gets the index as user data. */
#define UI_SEGMENTS_MAX 3
typedef struct {
    lv_obj_t *btn[UI_SEGMENTS_MAX];
    int count;
} ui_segments_t;
ui_segments_t ui_setting_segments(lv_obj_t *page, int32_t y, const char *const *labels, int count, int32_t w, lv_event_cb_t cb);
void ui_segments_select(const ui_segments_t *s, int selected);

/* ---- status line: a dot and a one-line name (the capture / IR lines) ---------------- */

/* A one-line label with an ellipsis: LONG_DOT needs a fixed height. */
lv_obj_t *ui_line_label(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, const lv_font_t *font, uint32_t color);
/* name ("" = `empty`) in the on / off colours, the dot green or dim. */
void ui_line_set(lv_obj_t *dot, lv_obj_t *label, const char *name, bool on, const char *empty);

/* "+2.1 dB" from tenths of a dB. */
void ui_format_db_tenths(int tenths, char *out, size_t cap);

#endif
