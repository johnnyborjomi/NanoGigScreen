/*
 * IR list: every IR on the pedal, as Cortex Cloud's IR loader lists them (not just the five on the pedal's IR list).
 * Tabs User / Factory in the header, six IRs a page; tap one to load it into the preset (a live edit), the list
 * stays open to try the next. The current IR has a green outline, the preset's saved one teal text. The app reads
 * the library on open (nano_ui_set_ir_library); it is freed on close. Built on open; "<" goes back to the IR tab.
 */
#include <stdlib.h>
#include <string.h>

#include "ui_internal.h"
#include "ui_widgets.h"

#define ROWS 6
#define ROW_GAP 4

static const int LISTS[2] = { NANO_IR_USER, NANO_IR_FACTORY }; /* tab order */

static struct {
    nano_ir_library_t *lib; /* owned; NULL while the pedal has not answered */
    int tab;                /* 0 = User, 1 = Factory */
} s;

static struct {
    bool built;
    lv_obj_t *tabs[2], *rows[ROWS], *names[ROWS], *note;
    ui_pager_t pager;
} w;

static int list_of_tab(void) { return LISTS[s.tab]; }
static int count(void) { return s.lib ? s.lib->count[list_of_tab()] : 0; }

static void on_back(lv_event_t *e) { (void)e; ui_go(NANO_VIEW_IR); }
static void on_close(lv_event_t *e) { (void)e; ui_go_base(); }

static void on_row(lv_event_t *e)
{
    int idx = w.pager.current * ROWS + (int)(intptr_t)lv_event_get_user_data(e);
    const char *name = nano_ir_library_name(s.lib, list_of_tab(), idx);
    if (!name || !name[0] || !g_ui.cb.on_ir_pick) return;
    g_ui.cb.on_ir_pick((uint8_t)list_of_tab(), (uint16_t)idx, name);
}

static void show_page(int page)
{
    const char *current = ir_tab_name();
    for (int i = 0; i < ROWS; i++) {
        const char *name = nano_ir_library_name(s.lib, list_of_tab(), page * ROWS + i);
        bool shown = name && name[0];
        lv_obj_set_hidden(w.rows[i], !shown);
        if (!shown) continue;
        lv_label_set_text(w.names[i], name);
        lv_obj_set_style_border_width(w.rows[i], strcmp(name, current) == 0 ? 2 : 0, 0);
        lv_obj_set_style_text_color(w.names[i], lv_color_hex(ir_is_saved(name) ? C_SAVED : C_TEXT), 0);
    }
    lv_label_set_text(w.note, !s.lib ? "Reading the pedal's IRs..." : count() ? "" : s.tab == 0 ? "No user IRs" : "No factory IRs");
}

/* The page holding the current IR in this tab, else the first. */
static int page_of_current(void)
{
    const char *current = ir_tab_name();
    for (int i = 0; i < count(); i++) {
        if (strcmp(nano_ir_library_name(s.lib, list_of_tab(), i), current) == 0) return i / ROWS;
    }
    return 0;
}

static void select_tab(int tab, bool to_current)
{
    s.tab = tab;
    ui_header_tabs_select(w.tabs, tab);
    int pages = (count() + ROWS - 1) / ROWS;
    ui_pager_set_count(&w.pager, pages ? pages : 1, to_current ? page_of_current() : 0);
}

static void on_tab(lv_event_t *e) { select_tab((int)(intptr_t)lv_event_get_user_data(e), true); }

static lv_obj_t *build(lv_obj_t *scr)
{
    ui_overlay_t o = ui_overlay(scr, "", on_back, on_close);
    static const char *const NAMES[2] = { "User", "Factory" };
    ui_header_tabs(o.root, NAMES, on_tab, w.tabs);
    const int32_t top = TOP_H + 6, h = SCREEN_H - top - 6, row_h = (h - (ROWS - 1) * ROW_GAP) / ROWS;
    ui_pager_create(&w.pager, o.root, top, h, "Page");
    w.pager.on_show = show_page;
    w.pager.wrap = true;
    for (int i = 0; i < ROWS; i++) {
        lv_obj_t *row = ui_box(o.root, SETTING_X, top + i * (row_h + ROW_GAP), SETTING_RIGHT, row_h, C_PANEL);
        lv_obj_set_style_radius(row, 8, 0);
        lv_obj_set_style_border_color(row, lv_color_hex(C_ON), 0);
        lv_obj_set_clickable(row, true);
        lv_obj_set_style_bg_color(row, lv_color_hex(C_TEXT), LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(row, LV_OPA_30, LV_STATE_PRESSED);
        lv_obj_add_event_cb(row, on_row, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        w.names[i] = ui_line_label(row, 8, 0, SETTING_RIGHT - 16, &lv_font_montserrat_14, C_TEXT);
        lv_obj_align(w.names[i], LV_ALIGN_LEFT_MID, 8, 0);
        lv_obj_set_hidden(row, true);
        w.rows[i] = row;
    }
    w.note = ui_label(o.root, &lv_font_montserrat_14, C_MUTED);
    lv_obj_set_pos(w.note, SETTING_X + 8, top + 8);
    w.built = true;
    return o.root;
}

static void destroy(void)
{
    w.built = false;
    free(s.lib);
    s.lib = NULL;
}

static void enter(nano_view_t view, nano_view_t from, bool notify)
{
    (void)view, (void)from, (void)notify;
    select_tab(0, true);
    if (g_ui.cb.on_ir_library) g_ui.cb.on_ir_library(true);
}

static void leave(nano_view_t to, bool notify)
{
    (void)to, (void)notify;
    if (g_ui.cb.on_ir_library) g_ui.cb.on_ir_library(false);
}

ui_page_t page_ir_list = { .build = build, .destroy = destroy, .enter = enter, .leave = leave, .needs_link = true };

void ir_list_refresh(void)
{
    if (w.built) show_page(w.pager.current);
}

/* Opens on the tab holding the current IR (User when it is in neither). */
void nano_ui_set_ir_library(nano_ir_library_t *lib)
{
    if (!w.built) {
        free(lib);
        return;
    }
    free(s.lib);
    s.lib = lib;
    const char *current = ir_tab_name();
    int tab = 0;
    for (int i = 0; lib && i < lib->count[NANO_IR_FACTORY]; i++) {
        if (strcmp(nano_ir_library_name(lib, NANO_IR_FACTORY, i), current) == 0) tab = 1;
    }
    select_tab(tab, true);
}
