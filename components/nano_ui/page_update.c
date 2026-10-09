/*
 * Firmware update over Wi-Fi: the installed version, the Wi-Fi network (Change), the update's status with its
 * action (Install / Check again / Try again) and progress; a network picker and a password panel with a keyboard.
 * Bluetooth is off while it shows; closing restarts the screen. Built on the first open and kept (the restart ends
 * it anyway); only the user leaves it.
 */
#include <stdio.h>
#include <string.h>

#include "ui_internal.h"
#include "ui_widgets.h"

#define UPD_X 12
#define UPD_W (SCREEN_W - 2 * UPD_X)
#define UPD_ROW_H 36
#define UPD_PASS_TA_H 36
#define UPD_KB_Y (TOP_H + 4 + 4 + UPD_PASS_TA_H + 22)
#define NETWORKS_MAX 10

typedef enum { PANEL_MAIN, PANEL_NETWORKS, PANEL_PASSWORD } panel_t;

static struct {
    panel_t panel;
    nano_update_state_t state;
    bool scanning;
    nano_ui_network_t networks[NETWORKS_MAX];
    int network_count;
    char pick[33];  /* network whose password is being typed */
} s = { .state = NANO_UPDATE_BUSY };

static struct {
    ui_overlay_t o;
    lv_obj_t *main, *version, *wifi, *text, *sub, *bar, *action;
    lv_obj_t *nets, *nets_title, *net_list;
    lv_obj_t *pass, *pass_title, *pass_ta;
} w;

/* The header "<": password -> networks, networks -> the update (only with a network to go back to). */
static void refresh_back(void)
{
    ui_overlay_set_back(&w.o, s.panel == PANEL_PASSWORD || (s.panel == PANEL_NETWORKS && g_ui.wifi_ssid[0]));
}

static void show_panel(panel_t p)
{
    s.panel = p;
    refresh_back();
    lv_obj_set_hidden(w.main, p != PANEL_MAIN);
    lv_obj_set_hidden(w.nets, p != PANEL_NETWORKS);
    lv_obj_set_hidden(w.pass, p != PANEL_PASSWORD);
}

static void refresh_wifi(void)
{
    lv_label_set_text(w.wifi, g_ui.wifi_ssid[0] ? g_ui.wifi_ssid : "Not set up");
    lv_obj_set_style_text_color(w.wifi, lv_color_hex(g_ui.wifi_ssid[0] ? C_TEXT : C_MUTED), 0);
    refresh_back();
}

static void start_join(const char *ssid, const char *password)
{
    strlcpy(g_ui.wifi_ssid, ssid, sizeof(g_ui.wifi_ssid));
    refresh_wifi();
    char t[64];
    snprintf(t, sizeof(t), "Connecting to %s", ssid);
    nano_ui_update_status(NANO_UPDATE_BUSY, t, 0);
    show_panel(PANEL_MAIN);
    if (g_ui.cb.on_wifi_join) g_ui.cb.on_wifi_join(ssid, password);
}

/* ---- events ------------------------------------------------------------------------- */

static void on_close(lv_event_t *e) { (void)e; ui_go_base(); }

static void on_change_wifi(lv_event_t *e)
{
    (void)e;
    nano_ui_update_show_networks(NULL, 0, true);
    if (g_ui.cb.on_wifi_scan) g_ui.cb.on_wifi_scan();
}

static void on_back(lv_event_t *e)
{
    (void)e;
    show_panel(s.panel == PANEL_PASSWORD ? PANEL_NETWORKS : PANEL_MAIN);
}

static void on_action(lv_event_t *e)
{
    (void)e;
    if (s.state == NANO_UPDATE_AVAILABLE) {
        nano_ui_update_status(NANO_UPDATE_DOWNLOADING, NULL, 0);
        if (g_ui.cb.on_update_install) g_ui.cb.on_update_install();
    } else if (s.state == NANO_UPDATE_UP_TO_DATE || s.state == NANO_UPDATE_ERROR) {
        nano_ui_update_status(NANO_UPDATE_BUSY, "Checking for updates", 0);
        if (g_ui.cb.on_update_check) g_ui.cb.on_update_check();
    }
}

static void on_network(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (i < 0 || i >= s.network_count) return;
    const nano_ui_network_t *n = &s.networks[i];
    if (!n->secure) {
        start_join(n->ssid, "");
        return;
    }
    strlcpy(s.pick, n->ssid, sizeof(s.pick));
    char t[64];
    snprintf(t, sizeof(t), "Password for %s", n->ssid);
    lv_label_set_text(w.pass_title, t);
    lv_textarea_set_text(w.pass_ta, "");
    show_panel(PANEL_PASSWORD);
}

static void on_eye(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target_obj(e);
    bool hidden = !lv_textarea_get_password_mode(w.pass_ta);
    lv_textarea_set_password_mode(w.pass_ta, hidden);
    lv_label_set_text(lv_obj_get_child(btn, 0), hidden ? LV_SYMBOL_EYE_OPEN : LV_SYMBOL_EYE_CLOSE);
}

static void on_keyboard_ready(lv_event_t *e)
{
    (void)e;
    const char *pw = lv_textarea_get_text(w.pass_ta);
    if (strlen(pw) < 8) return; /* WPA2 needs 8 or more: keep typing */
    start_join(s.pick, pw);
}

/* ---- build ------------------------------------------------------------------------- */

/* Four bars, lit by signal strength. */
static void make_bars(lv_obj_t *parent, int32_t x, int32_t y, int8_t rssi)
{
    int lit = rssi > -55 ? 4 : rssi > -65 ? 3 : rssi > -75 ? 2 : 1;
    for (int i = 0; i < 4; i++) {
        int32_t h = 4 + i * 3;
        lv_obj_t *b = ui_box(parent, x + i * 5, y + 13 - h, 3, h, i < lit ? C_TEXT : C_DIM);
        lv_obj_set_clickable(b, false);
    }
}

static lv_obj_t *small_button(lv_obj_t *parent, int32_t y, const char *text)
{
    lv_obj_t *b = ui_button(parent, SCREEN_W - UPD_X - 76, y, 76, 28, text, &lv_font_montserrat_12, C_PANEL, C_ACCENT, on_change_wifi, NULL);
    lv_obj_set_style_radius(b, 7, 0);
    return b;
}

static void build_main_panel(lv_obj_t *root, int32_t top)
{
    /* Installed version, Wi-Fi line, status, action. */
    w.main = ui_box(root, 0, top, SCREEN_W, SCREEN_H - top, C_BG);
    lv_obj_t *l = ui_label(w.main, &lv_font_montserrat_12, C_MUTED);
    lv_label_set_text(l, "Installed");
    lv_obj_set_pos(l, UPD_X, 8);
    w.version = ui_label(w.main, &lv_font_montserrat_14, C_TEXT);
    lv_obj_set_pos(w.version, 76, 6);
    lv_obj_set_width(w.version, UPD_W - 64);
    lv_label_set_long_mode(w.version, LV_LABEL_LONG_DOT);
    lv_label_set_text(w.version, g_ui.fw_version);
    l = ui_label(w.main, &lv_font_montserrat_12, C_MUTED);
    lv_label_set_text(l, "Wi-Fi");
    lv_obj_set_pos(l, UPD_X, 38);
    w.wifi = ui_label(w.main, &lv_font_montserrat_14, C_TEXT);
    lv_obj_set_pos(w.wifi, 76, 36);
    lv_obj_set_width(w.wifi, UPD_W - 64 - 84);
    lv_label_set_long_mode(w.wifi, LV_LABEL_LONG_DOT);
    lv_obj_set_ext_click_area(small_button(w.main, 30, "Change"), 6);
    ui_box(w.main, UPD_X, 66, UPD_W, 1, C_PANEL_2);

    w.text = ui_label(w.main, &lv_font_montserrat_20, C_TEXT);
    lv_obj_set_width(w.text, UPD_W);
    lv_obj_set_pos(w.text, UPD_X, 78);
    lv_obj_set_style_text_align(w.text, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(w.text, LV_LABEL_LONG_DOT);
    lv_obj_set_height(w.text, lv_font_get_line_height(&lv_font_montserrat_20));
    w.sub = ui_label(w.main, &lv_font_montserrat_12, C_MUTED);
    lv_obj_set_width(w.sub, UPD_W);
    lv_obj_set_pos(w.sub, UPD_X, 106);
    lv_obj_set_style_text_align(w.sub, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(w.sub, LV_LABEL_LONG_WRAP);
    w.bar = lv_bar_create(w.main);
    lv_obj_set_pos(w.bar, UPD_X + 12, 136);
    lv_obj_set_size(w.bar, UPD_W - 24, 10);
    lv_bar_set_range(w.bar, 0, 100);
    lv_obj_set_style_bg_color(w.bar, lv_color_hex(C_PANEL_2), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(w.bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(w.bar, lv_color_hex(C_ON), LV_PART_INDICATOR);
    lv_obj_set_style_radius(w.bar, 5, LV_PART_MAIN);
    lv_obj_set_style_radius(w.bar, 5, LV_PART_INDICATOR);
    w.action = ui_button(w.main, UPD_X, SCREEN_H - top - 54, UPD_W, 44, "", &lv_font_montserrat_20, C_ACCENT, C_FX_TEXT, on_action, NULL);
}

static void build_networks_panel(lv_obj_t *root, int32_t top)
{
    w.nets = ui_box(root, 0, top, SCREEN_W, SCREEN_H - top, C_BG);
    w.nets_title = ui_label(w.nets, &lv_font_montserrat_14, C_TEXT);
    lv_label_set_text(w.nets_title, "Choose Wi-Fi");
    lv_obj_set_pos(w.nets_title, UPD_X, 8);
    small_button(w.nets, 2, LV_SYMBOL_REFRESH " Scan");
    w.net_list = ui_box(w.nets, UPD_X, 36, UPD_W, SCREEN_H - top - 40, C_BG);
    lv_obj_set_scrollable(w.net_list, true);
    lv_obj_set_scroll_dir(w.net_list, LV_DIR_VER);
    lv_obj_set_flex_flow(w.net_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(w.net_list, 4, 0);
}

static void build_password_panel(lv_obj_t *root, int32_t top)
{
    /* Field + show / hide, keyboard under it (OK joins, the header "<" goes back). */
    w.pass = ui_box(root, 0, top, SCREEN_W, SCREEN_H - top, C_BG);
    w.pass_title = ui_line_label(w.pass, UPD_X, 2, UPD_W, &lv_font_montserrat_12, C_MUTED);
    w.pass_ta = ui_text_field(w.pass, UPD_X, 20, UPD_W - 48, UPD_PASS_TA_H, &lv_font_montserrat_14);
    lv_textarea_set_password_mode(w.pass_ta, true);
    lv_textarea_set_max_length(w.pass_ta, 63);
    lv_textarea_set_placeholder_text(w.pass_ta, "8 characters or more");
    lv_obj_t *eye = ui_button(w.pass, SCREEN_W - UPD_X - 42, 20, 42, UPD_PASS_TA_H, LV_SYMBOL_EYE_OPEN, &lv_font_montserrat_14, C_PANEL, C_MUTED, on_eye, NULL);
    lv_obj_set_style_radius(eye, 7, 0);
    lv_obj_t *kb = ui_keyboard(w.pass, SCREEN_H - UPD_KB_Y, w.pass_ta);
    lv_obj_add_event_cb(kb, on_keyboard_ready, LV_EVENT_READY, NULL);
}

static lv_obj_t *build(lv_obj_t *scr)
{
    w.o = ui_overlay(scr, "Firmware update", on_back, on_close);
    const int32_t top = TOP_H + 4;
    build_main_panel(w.o.root, top);
    build_networks_panel(w.o.root, top);
    build_password_panel(w.o.root, top);
    refresh_wifi();
    nano_ui_update_status(NANO_UPDATE_BUSY, "Starting Wi-Fi", 0);
    show_panel(PANEL_MAIN);
    return w.o.root;
}

static void enter(nano_view_t view, nano_view_t from, bool notify)
{
    (void)view, (void)from;
    if (notify && g_ui.cb.on_update_open) g_ui.cb.on_update_open();
}

static void leave(nano_view_t to, bool notify)
{
    (void)to;
    if (notify && g_ui.cb.on_update_close) g_ui.cb.on_update_close();
}

ui_page_t page_update = { .build = build, .enter = enter, .leave = leave, .keep = true };

/* ---- the app's side --------------------------------------------------------------------- */

void nano_ui_set_firmware_version(const char *version)
{
    strlcpy(g_ui.fw_version, version && version[0] ? version : "unknown", sizeof(g_ui.fw_version));
    settings_refresh();
    if (w.version) lv_label_set_text(w.version, g_ui.fw_version);
}

void nano_ui_update_set_wifi(const char *ssid)
{
    strlcpy(g_ui.wifi_ssid, ssid ? ssid : "", sizeof(g_ui.wifi_ssid));
    if (w.o.root) refresh_wifi();
}

void nano_ui_update_status(nano_update_state_t state, const char *text, int percent)
{
    if (!w.o.root) return;
    s.state = state;
    char t[64] = "", sub[96] = "";
    const char *action = NULL;
    uint32_t color = C_TEXT;
    switch (state) {
    case NANO_UPDATE_BUSY:
        snprintf(t, sizeof(t), "%s", text && text[0] ? text : "Working");
        snprintf(sub, sizeof(sub), "Bluetooth is off until you close this page");
        color = C_MUTED;
        break;
    case NANO_UPDATE_UP_TO_DATE:
        snprintf(t, sizeof(t), LV_SYMBOL_OK " Up to date");
        snprintf(sub, sizeof(sub), "Latest release: %s", text ? text : "");
        color = C_ON;
        action = "Check again";
        break;
    case NANO_UPDATE_AVAILABLE:
        snprintf(t, sizeof(t), "Update available");
        snprintf(sub, sizeof(sub), "Version %s", text ? text : "");
        color = C_ACCENT;
        action = LV_SYMBOL_DOWNLOAD "  Install";
        break;
    case NANO_UPDATE_DOWNLOADING:
        snprintf(t, sizeof(t), "Installing  %d%%", percent);
        snprintf(sub, sizeof(sub), "Keep the screen powered");
        break;
    case NANO_UPDATE_DONE:
        snprintf(t, sizeof(t), LV_SYMBOL_OK " Installed");
        snprintf(sub, sizeof(sub), "Restarting");
        color = C_ON;
        break;
    case NANO_UPDATE_ERROR:
        snprintf(t, sizeof(t), "%s", text && text[0] ? text : "Something went wrong");
        snprintf(sub, sizeof(sub), "Check the Wi-Fi network, then try again");
        color = C_ERROR;
        action = "Try again";
        break;
    }
    lv_label_set_text(w.text, t);
    lv_obj_set_style_text_color(w.text, lv_color_hex(color), 0);
    lv_label_set_text(w.sub, sub);
    lv_obj_set_hidden(w.bar, state != NANO_UPDATE_DOWNLOADING && state != NANO_UPDATE_DONE);
    lv_bar_set_value(w.bar, state == NANO_UPDATE_DONE ? 100 : percent, LV_ANIM_OFF);
    lv_obj_set_hidden(w.action, action == NULL);
    if (action) {
        lv_obj_t *al = lv_obj_get_child(w.action, 0);
        lv_label_set_text(al, action);
        lv_obj_center(al);
    }
    /* Closing restarts the screen: not while the new image is being written or activated. A check that was already
     * running reports here while the user picks a network: it updates the main panel behind the list instead of
     * pulling the list away (the "Change" flicker). */
    lv_obj_set_hidden(w.o.close, state == NANO_UPDATE_DOWNLOADING || state == NANO_UPDATE_DONE);
}

static void add_network_row(int i)
{
    const nano_ui_network_t *n = &s.networks[i];
    lv_obj_t *row = ui_button(w.net_list, 0, 0, UPD_W, UPD_ROW_H, "", &lv_font_montserrat_14, C_PANEL, C_TEXT, on_network, (void *)(intptr_t)i);
    lv_obj_set_style_radius(row, 8, 0);
    lv_obj_t *name = lv_obj_get_child(row, 0);
    lv_label_set_text(name, n->ssid);
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
    lv_obj_set_size(name, UPD_W - 70, lv_font_get_line_height(&lv_font_montserrat_14)); /* one line: LONG_DOT needs a fixed height */
    lv_obj_align(name, LV_ALIGN_LEFT_MID, 10, 0);
    if (n->secure) {
        lv_obj_t *lock = ui_label(row, &montserrat_medium_10, C_MUTED);
        lv_label_set_text(lock, "WPA");
        lv_obj_align(lock, LV_ALIGN_RIGHT_MID, -32, 0);
    }
    make_bars(row, UPD_W - 28, (UPD_ROW_H - 13) / 2, n->rssi);
}

void nano_ui_update_show_networks(const nano_ui_network_t *networks, int count, bool scanning)
{
    if (!w.o.root) return;
    /* A scan in progress: the title says so and the last list stays (no blank, no rebuild). The tap and the scan's
     * own start both report it: the second one changes nothing. */
    lv_label_set_text(w.nets_title, scanning ? "Searching..." : "Choose Wi-Fi");
    bool had_list = s.network_count > 0;
    if (scanning) {
        bool again = s.scanning;
        s.scanning = true;
        if (s.panel != PANEL_PASSWORD) show_panel(PANEL_NETWORKS);
        if (again || had_list) return;
    } else {
        s.scanning = false;
    }
    if (count > NETWORKS_MAX) count = NETWORKS_MAX;
    s.network_count = scanning || !networks ? 0 : count;
    if (s.network_count) memcpy(s.networks, networks, sizeof(*networks) * (size_t)s.network_count);
    lv_obj_clean(w.net_list);
    if (!s.network_count) {
        lv_obj_t *l = ui_label(w.net_list, &lv_font_montserrat_14, C_MUTED);
        lv_label_set_text(l, scanning ? "Searching for networks" : "No networks found");
    }
    for (int i = 0; i < s.network_count; i++) add_network_row(i);
    if (s.panel != PANEL_PASSWORD) show_panel(PANEL_NETWORKS); /* never from under the typing */
}
