/*
 * Presets list: one bank per page (Settings > Presets per bank), the pager steps through the banks and wraps.
 * Tap a row to select that preset, hold it to rename (the rename page's "<" comes back to this bank). Built on
 * open: ~10 KB of heap the gig needs more.
 */
#include <stdio.h>

#include "ui_internal.h"
#include "ui_widgets.h"

#define ROWS_MAX 8
#define ROW_GAP 4
#define ROW_H_MAX 48

static struct {
    ui_pager_t pager;
    lv_obj_t *rows[ROWS_MAX], *tags[ROWS_MAX], *names[ROWS_MAX];
    int32_t top, h;
} w;
static bool s_built;
static int s_return_bank = -1; /* reopen on this bank (back from renaming one of its presets) */

static void on_close(lv_event_t *e) { (void)e; ui_go_base(); }

static int row_preset(lv_event_t *e)
{
    int idx = w.pager.current * g_ui.per_bank + (int)(uintptr_t)lv_event_get_user_data(e);
    return idx < NANO_PRESET_COUNT ? idx : -1;
}

static void on_row_clicked(lv_event_t *e)
{
    int idx = row_preset(e);
    if (idx < 0) return;
    if (g_ui.cb.on_select_preset) g_ui.cb.on_select_preset((uint8_t)idx);
    ui_go_base();
}

static void on_row_long(lv_event_t *e)
{
    int idx = row_preset(e);
    if (idx < 0) return;
    s_return_bank = w.pager.current;
    nano_ui_open_rename((uint8_t)idx);
}

/* Fill the rows for one bank: as many as presets per bank, sized to share the page height. */
static void show_bank(int bank)
{
    int n = g_ui.per_bank;
    int32_t h = (w.h - (n - 1) * ROW_GAP) / n;
    if (h > ROW_H_MAX) h = ROW_H_MAX;
    const lv_font_t *font = h >= 40 ? &lv_font_montserrat_20 : &lv_font_montserrat_14;
    /* Names line up after the widest label of the bank ("4C", "22A", "64"). */
    int32_t tag_w = 0;
    for (int i = 0; i < n && bank * n + i < NANO_PRESET_COUNT; i++) {
        char label[8];
        nano_preset_label((uint8_t)(bank * n + i), g_ui.per_bank, g_ui.label_style, label, sizeof(label));
        lv_point_t size;
        lv_text_get_size(&size, label, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        if (size.x > tag_w) tag_w = size.x;
    }
    tag_w += 10;
    for (int i = 0; i < ROWS_MAX; i++) {
        int idx = bank * n + i;
        lv_obj_t *row = w.rows[i];
        bool shown = i < n && idx < NANO_PRESET_COUNT;
        lv_obj_set_hidden(row, !shown);
        if (!shown) continue;
        lv_obj_set_pos(row, SETTING_X, w.top + i * (h + ROW_GAP));
        lv_obj_set_height(row, h);
        /* The shown preset: a green outline. */
        lv_obj_set_style_border_width(row, idx == g_ui.preset ? 2 : 0, 0);

        char label[8];
        nano_preset_label((uint8_t)idx, g_ui.per_bank, g_ui.label_style, label, sizeof(label));
        lv_obj_t *tag = w.tags[i];
        lv_label_set_text(tag, label);
        lv_obj_set_style_text_font(tag, font, 0);
        lv_obj_set_style_text_color(tag, lv_color_hex(UI_SLOT_COLORS[i & 7]), 0);
        lv_obj_align(tag, LV_ALIGN_LEFT_MID, 8, 0);

        char fallback[24];
        lv_obj_t *nl = w.names[i];
        lv_obj_set_style_text_font(nl, font, 0);
        lv_obj_set_size(nl, SETTING_RIGHT - 8 - tag_w - 8, lv_font_get_line_height(font));
        lv_label_set_text(nl, ui_preset_name((uint8_t)idx, fallback, sizeof(fallback)));
        lv_obj_align(nl, LV_ALIGN_LEFT_MID, 8 + tag_w, 0);
    }
}

static lv_obj_t *build(lv_obj_t *scr)
{
    ui_overlay_t o = ui_overlay(scr, "Presets", on_close, on_close);
    w.top = TOP_H + 6;
    w.h = SCREEN_H - w.top - 6;
    ui_pager_create(&w.pager, o.root, w.top, w.h, "Bank");
    w.pager.on_show = show_bank;
    w.pager.wrap = true; /* like prev / next on the gig view: bank 16 -> bank 1 */
    for (int i = 0; i < ROWS_MAX; i++) {
        lv_obj_t *row = ui_box(o.root, SETTING_X, w.top, SETTING_RIGHT, ROW_H_MAX, C_PANEL);
        lv_obj_set_style_radius(row, 8, 0);
        lv_obj_set_style_border_color(row, lv_color_hex(C_ON), 0);
        lv_obj_set_clickable(row, true);
        lv_obj_set_style_bg_color(row, lv_color_hex(C_TEXT), LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(row, LV_OPA_30, LV_STATE_PRESSED);
        lv_obj_add_event_cb(row, on_row_clicked, LV_EVENT_SHORT_CLICKED, (void *)(uintptr_t)i);
        lv_obj_add_event_cb(row, on_row_long, LV_EVENT_LONG_PRESSED, (void *)(uintptr_t)i);
        w.tags[i] = ui_label(row, &lv_font_montserrat_14, C_TEXT);
        w.names[i] = ui_label(row, &lv_font_montserrat_14, C_TEXT);
        lv_label_set_long_mode(w.names[i], LV_LABEL_LONG_DOT);
        lv_obj_set_hidden(row, true);
        w.rows[i] = row;
    }
    s_built = true;
    return o.root;
}

static void destroy(void) { s_built = false; }

/* Open on the bank of the shown preset, or the one a rename started from. */
static void enter(nano_view_t view, nano_view_t from, bool notify)
{
    (void)view, (void)from, (void)notify;
    int banks = (NANO_PRESET_COUNT + g_ui.per_bank - 1) / g_ui.per_bank;
    int bank = s_return_bank >= 0 && s_return_bank < banks ? s_return_bank : g_ui.preset / g_ui.per_bank;
    s_return_bank = -1;
    ui_pager_set_count(&w.pager, banks, bank);
}

/* The list returns to a bank only straight back from renaming one of its presets (not after ✕ or a drop). */
static void leave(nano_view_t to, bool notify)
{
    (void)notify;
    if (to != NANO_VIEW_RENAME) s_return_bank = -1;
}

ui_page_t page_presets = { .build = build, .destroy = destroy, .enter = enter, .leave = leave };

void presets_refresh(void)
{
    if (s_built && g_ui.view == NANO_VIEW_PRESETS) show_bank(w.pager.current);
}

void presets_forget_return_bank(void)
{
    s_return_bank = -1;
}
