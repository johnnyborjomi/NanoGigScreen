/* Menu: Disconnect / Connect, Settings, Tempo, Tuner; one colour per item so a row is found without reading it. */
#include "ui_internal.h"
#include "ui_widgets.h"

#define C_TUNER_ITEM 0xBFE3FF

static struct {
    lv_obj_t *link_label;
} w;

static void on_close(lv_event_t *e) { (void)e; ui_go_base(); }
static void on_open_settings(lv_event_t *e) { (void)e; ui_go(NANO_VIEW_SETTINGS); }
static void on_open_tuner(lv_event_t *e) { (void)e; ui_go(NANO_VIEW_TUNER); }
static void on_open_tempo(lv_event_t *e) { (void)e; ui_go(NANO_VIEW_TEMPO); }

static void on_link_toggle(lv_event_t *e)
{
    (void)e;
    bool enable = !g_ui.link_enabled;
    nano_ui_set_link_enabled(enable);
    if (g_ui.cb.on_link) g_ui.cb.on_link(enable);
    ui_go_base();
}

static lv_obj_t *menu_item(lv_obj_t *parent, int row, const char *text, uint32_t color, lv_event_cb_t cb)
{
    const int32_t h = 44, gap = 6, y0 = 30;
    return ui_button(parent, 12, y0 + row * (h + gap), SCREEN_W - 24, h, text, &lv_font_montserrat_20, C_PANEL, color, cb, NULL);
}

static lv_obj_t *build(lv_obj_t *scr)
{
    ui_overlay_t o = ui_overlay(scr, "NanoGig Screen", NULL, on_close);
    w.link_label = lv_obj_get_child(menu_item(o.root, 0, "", C_ERROR, on_link_toggle), 0);
    menu_item(o.root, 1, LV_SYMBOL_SETTINGS "  Settings", C_WARN, on_open_settings);
    menu_item(o.root, 2, LV_SYMBOL_LOOP "  Tempo", C_ON, on_open_tempo);
    menu_item(o.root, 3, LV_SYMBOL_AUDIO "  Tuner", C_TUNER_ITEM, on_open_tuner);
    menu_refresh_link();
    return o.root;
}

static void destroy(void) { w.link_label = NULL; }

ui_page_t page_menu = { .build = build, .destroy = destroy };

void menu_refresh_link(void)
{
    if (!w.link_label) return;
    bool enabled = g_ui.link_enabled;
    lv_label_set_text(w.link_label, enabled ? LV_SYMBOL_BLUETOOTH "  Disconnect" : LV_SYMBOL_BLUETOOTH "  Connect");
    lv_obj_set_style_text_color(w.link_label, lv_color_hex(enabled ? C_ERROR : C_TEXT), 0);
    lv_obj_center(w.link_label);
}
