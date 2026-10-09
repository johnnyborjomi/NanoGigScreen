/*
 * nano_ui: creation, page switching and the setters several pages share. The pages themselves are page_*.c (see
 * ui_internal.h for how a page is put together).
 */
#include "nano_ui.h"

#include <stdio.h>
#include <string.h>

#include "ui_internal.h"

const uint32_t UI_SLOT_COLORS[8] = { 0xFF5C5C, 0xFFB454, 0x4CF06A, 0x00F0D8, 0x3D9BFF, 0xA78BFA, 0xF050C8, 0xF4F6F8 };

ui_t g_ui = {
    .view = NANO_VIEW_MAIN,
    .base_view = NANO_VIEW_CONNECT,
    .per_bank = 4,
    .label_style = NANO_LABEL_NUMBER_LETTER,
    .link_enabled = true,
    .exp_show = true,
    .fw_version = "unknown",
};

static ui_page_t *const PAGES[] = {
    [NANO_VIEW_MAIN] = &page_main,
    [NANO_VIEW_MENU] = &page_menu,
    [NANO_VIEW_SETTINGS] = &page_settings,
    [NANO_VIEW_TUNER] = &page_tuner,
    [NANO_VIEW_TEMPO] = &page_tempo,
    [NANO_VIEW_CONNECT] = &page_connect,
    [NANO_VIEW_UPDATE] = &page_update,
    [NANO_VIEW_PRESETS] = &page_presets,
    [NANO_VIEW_CAPTURE] = &page_source,
    [NANO_VIEW_IR] = &page_source,
    [NANO_VIEW_RENAME] = &page_rename,
};

/* ---- page switching ---------------------------------------------------------------- */

bool ui_page_built(const ui_page_t *page) { return page->root != NULL; }

static void page_close(ui_page_t *p)
{
    if (!p->root || p->underlay) return;
    lv_obj_set_hidden(p->root, true);
    if (p->keep) return;
    /* Async: the tap that closes it is still being handled by one of its children. */
    lv_obj_delete_async(p->root);
    p->root = NULL;
    if (p->destroy) p->destroy();
}

static void page_open(ui_page_t *p)
{
    if (!p->root) p->root = p->build(g_ui.scr);
    lv_obj_set_hidden(p->root, false);
}

void ui_show_view(nano_view_t view, bool notify)
{
    if (view == g_ui.view || (unsigned)view >= sizeof(PAGES) / sizeof(PAGES[0])) return;
    /* Only the user leaves the update view (it ends in a restart); late pedal events must not. */
    if (g_ui.view == NANO_VIEW_UPDATE && !notify) return;
    nano_view_t from = g_ui.view;
    ui_page_t *a = PAGES[from], *b = PAGES[view];
    if (a != b) {
        if (a->leave) a->leave(view, notify);
        page_close(a);
    }
    g_ui.view = view;
    if (a != b) page_open(b);
    if (b->enter) b->enter(view, from, notify);
}

void ui_go(nano_view_t view) { ui_show_view(view, true); }
void ui_go_base(void) { ui_show_view(g_ui.base_view, true); }

void nano_ui_show(nano_view_t view) { ui_show_view(view, true); }
nano_view_t nano_ui_view(void) { return g_ui.view; }
void nano_ui_open_tuner_from_pedal(void) { ui_show_view(NANO_VIEW_TUNER, false); }
void nano_ui_open_tempo_from_pedal(void) { ui_show_view(NANO_VIEW_TEMPO, false); }
void nano_ui_close_from_pedal(void) { ui_show_view(g_ui.base_view, false); }

void nano_ui_set_connected(bool live)
{
    g_ui.base_view = live ? NANO_VIEW_MAIN : NANO_VIEW_CONNECT;
    if (live && g_ui.view == NANO_VIEW_CONNECT) ui_show_view(NANO_VIEW_MAIN, false);
    /* Down: the pedal's pages make no claims any more; the menu and settings can stay open. */
    if (!live && PAGES[g_ui.view]->needs_link) ui_show_view(NANO_VIEW_CONNECT, false);
}

/* ---- create ------------------------------------------------------------------------- */

void nano_ui_create(lv_display_t *disp, const nano_ui_callbacks_t *cb)
{
    g_ui.cb = *cb;
    g_ui.scr = lv_display_get_screen_active(disp);
    lv_obj_set_style_bg_color(g_ui.scr, lv_color_hex(C_BG), 0);
    lv_obj_set_style_bg_opa(g_ui.scr, LV_OPA_COVER, 0);
    lv_obj_set_scrollable(g_ui.scr, false);
    ui_log_presses(disp);
    /* Built now: the gig view, and the connect page whose status line follows the link from the start. */
    page_main.root = page_main.build(g_ui.scr);
    page_connect.root = page_connect.build(g_ui.scr);
    nano_ui_set_link_enabled(true);
    nano_ui_set_status("Starting", false);
    nano_ui_set_connected(false);
}

/* ---- values several pages show --------------------------------------------------------- */

const char *ui_preset_name(uint8_t index, char *fallback, size_t cap)
{
    const char *name = g_ui.meta && index < NANO_PRESET_COUNT ? g_ui.meta->presets[index].name : "";
    if (name[0]) return name;
    snprintf(fallback, cap, "Preset %u", (unsigned)index + 1);
    return fallback;
}

void nano_ui_set_link_enabled(bool enabled)
{
    g_ui.link_enabled = enabled;
    menu_refresh_link();
    connect_refresh_link();
}

void nano_ui_set_bank_size(uint8_t per_bank)
{
    if (per_bank < 2 || per_bank > 8) return;
    g_ui.per_bank = per_bank;
    settings_refresh();
}

void nano_ui_set_label_style(uint8_t style)
{
    if (style > NANO_LABEL_NUMERIC) return;
    g_ui.label_style = (nano_label_style_t)style;
    settings_refresh();
}

void nano_ui_set_outputs_muted(bool muted)
{
    g_ui.outputs_muted = muted;
    main_refresh_mute();
    settings_refresh();
}

void nano_ui_set_expression_show(bool show)
{
    g_ui.exp_show = show;
    settings_refresh();
    main_refresh_expression();
}

/* ---- previews and tests ------------------------------------------------------------------- */

lv_obj_t *nano_ui_find(const char *text)
{
    return ui_find_text(g_ui.scr, text);
}

lv_obj_t *nano_ui_find_keyboard(void)
{
    return ui_find_class(g_ui.scr, &lv_keyboard_class);
}
