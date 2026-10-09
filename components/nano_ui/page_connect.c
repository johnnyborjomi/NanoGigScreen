/*
 * Connect page: shown while there is no live state. While scanning it shows the pedal's pairing gesture (EXIT +
 * CAPTURE, as Cortex Cloud does); after a deliberate disconnect, a Connect button. The status line at the bottom
 * follows the link. Built once (the status changes all the time).
 */
#include "ui_internal.h"

#define RING_D 62
#define RING_INNER_D 40
#define RING_GAP 44        /* room for the "+" between the rings */
#define C_RING_INNER 0x22B08A

static struct {
    lv_obj_t *title, *status, *dot, *pair, *btn, *free;
} w;

static void on_menu(lv_event_t *e) { (void)e; ui_go(NANO_VIEW_MENU); }

/* The connect button: enable the link (the app scans again). */
static void on_connect_clicked(lv_event_t *e)
{
    (void)e;
    if (g_ui.link_enabled) return;
    nano_ui_set_link_enabled(true);
    if (g_ui.cb.on_link) g_ui.cb.on_link(true);
}

static lv_obj_t *make_ring(lv_obj_t *parent, int32_t x, int32_t y, const char *caption)
{
    lv_obj_t *outer = ui_box(parent, x, y, RING_D, RING_D, C_PANEL);
    lv_obj_set_style_radius(outer, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(outer, 3, 0);
    lv_obj_set_style_border_color(outer, lv_color_hex(C_ON), 0);
    lv_obj_t *inner = ui_box(outer, 0, 0, RING_INNER_D, RING_INNER_D, C_RING_INNER);
    lv_obj_set_style_radius(inner, LV_RADIUS_CIRCLE, 0);
    lv_obj_center(inner);
    lv_obj_t *l = ui_label(parent, &montserrat_medium_12, C_TEXT);
    lv_label_set_text(l, caption);
    lv_obj_set_style_text_letter_space(l, 2, 0);
    lv_obj_set_width(l, RING_D + 40);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(l, x - 20, y + RING_D + 6);
    return outer;
}

/* A full-width centred label at `y`. */
static lv_obj_t *centred_label(lv_obj_t *parent, int32_t y, const lv_font_t *font, uint32_t color, const char *text)
{
    lv_obj_t *l = ui_label(parent, font, color);
    lv_label_set_text(l, text);
    lv_obj_set_width(l, SCREEN_W);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(l, 0, y);
    return l;
}

static void dot_opa_cb(void *obj, int32_t v) { lv_obj_set_style_opa((lv_obj_t *)obj, (lv_opa_t)v, 0); }

/* The status dot breathes while the page is showing (the only motion on a page that may sit for minutes). */
static void animate_dot(bool run)
{
    lv_anim_delete(w.dot, dot_opa_cb);
    lv_obj_set_style_opa(w.dot, LV_OPA_COVER, 0);
    if (!run) return;
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, w.dot);
    lv_anim_set_exec_cb(&a, dot_opa_cb);
    lv_anim_set_values(&a, LV_OPA_COVER, LV_OPA_20);
    lv_anim_set_duration(&a, 700);
    lv_anim_set_playback_duration(&a, 700);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&a);
}

static lv_obj_t *build(lv_obj_t *scr)
{
    lv_obj_t *root = ui_box(scr, 0, 0, SCREEN_W, SCREEN_H, C_BG);
    lv_obj_t *menu = ui_button(root, SCREEN_W - 44, 0, 44, TOP_H + 4, LV_SYMBOL_LIST, &lv_font_montserrat_14, C_BG, C_MUTED, on_menu, NULL);
    lv_obj_set_ext_click_area(menu, 6);

    w.title = ui_label(root, &lv_font_montserrat_14, C_TEXT);
    lv_obj_set_width(w.title, SCREEN_W - 88);
    lv_obj_set_style_text_align(w.title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(w.title, 44, 28);
    lv_label_set_text(w.title, "Put the pedal in connect mode");

    /* Scanning: the pairing gesture. */
    w.pair = ui_box(root, 0, 50, SCREEN_W, 150, C_BG);
    make_ring(w.pair, SCREEN_W / 2 - RING_GAP / 2 - RING_D, 0, "EXIT");
    make_ring(w.pair, SCREEN_W / 2 + RING_GAP / 2, 0, "CAPTURE");
    lv_obj_t *plus = ui_label(w.pair, &lv_font_montserrat_24, C_MUTED);
    lv_label_set_text(plus, "+");
    lv_obj_set_width(plus, RING_GAP);
    lv_obj_set_style_text_align(plus, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(plus, SCREEN_W / 2 - RING_GAP / 2, RING_D / 2 - 14);
    centred_label(w.pair, RING_D + 30, &lv_font_montserrat_14, C_TEXT, "Hold both for 2 seconds");
    lv_obj_t *note = ui_label(w.pair, &lv_font_montserrat_12, C_MUTED);
    lv_label_set_text(note, "The pedal pairs with one device at a time: close Cortex Cloud on your phone first.");
    lv_label_set_long_mode(note, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(note, SCREEN_W - 40);
    lv_obj_set_style_text_align(note, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(note, 20, RING_D + 54);

    /* Disconnected on purpose: one button brings the link back. */
    w.btn = ui_button(root, SCREEN_W / 2 - 80, 96, 160, 48, LV_SYMBOL_BLUETOOTH "  Connect", &lv_font_montserrat_20, C_ACCENT, C_FX_TEXT, on_connect_clicked, NULL);
    lv_obj_set_hidden(w.btn, true);
    w.free = centred_label(root, 96 + 48 + 14, &lv_font_montserrat_12, C_MUTED, "Cortex Cloud can use the pedal now");
    lv_obj_set_hidden(w.free, true);

    w.status = ui_label(root, &lv_font_montserrat_14, C_MUTED);
    lv_obj_align(w.status, LV_ALIGN_BOTTOM_MID, 8, -12);
    w.dot = ui_dot(root, 0, 0, 10);
    lv_obj_set_style_bg_color(w.dot, lv_color_hex(C_WARN), 0);
    lv_obj_set_hidden(root, true);
    return root;
}

static void enter(nano_view_t view, nano_view_t from, bool notify)
{
    (void)view, (void)from, (void)notify;
    animate_dot(g_ui.link_enabled);
}

static void leave(nano_view_t to, bool notify)
{
    (void)to, (void)notify;
    animate_dot(false);
}

ui_page_t page_connect = { .build = build, .enter = enter, .leave = leave, .keep = true };

void connect_set_status(const char *text, bool connected)
{
    lv_label_set_text(w.status, text);
    lv_obj_set_style_bg_color(w.dot, lv_color_hex(connected ? C_ON : g_ui.link_enabled ? C_WARN : C_DIM), 0);
    lv_obj_update_layout(w.status);
    lv_obj_align_to(w.dot, w.status, LV_ALIGN_OUT_LEFT_MID, -8, 0);
}

/* The pairing gesture while scanning, a Connect button after a deliberate disconnect. */
void connect_refresh_link(void)
{
    bool enabled = g_ui.link_enabled;
    lv_label_set_text(w.title, enabled ? "Put the pedal in connect mode" : "Disconnected from the pedal");
    lv_obj_set_hidden(w.pair, !enabled);
    lv_obj_set_hidden(w.btn, enabled);
    lv_obj_set_hidden(w.free, enabled);
    animate_dot(g_ui.view == NANO_VIEW_CONNECT && enabled);
}
