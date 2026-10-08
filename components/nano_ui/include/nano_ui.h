/*
 * The gig screen (LVGL 9, 320x240) and its overlays (menu, settings, tuner, tempo,
 * connect page).
 *
 * Main view: top bar (link status, tempo, gate, menu button), preset row
 * (prev button, bank label + name, next button), capture and IR lines, five
 * FX tiles in category colours. Tap a tile to toggle that block. LIST (right of
 * the capture / IR lines) opens the presets list; the capture line opens the capture page.
 *
 * Every call must hold the LVGL lock (lvgl_port_lock) except nano_ui_create,
 * which the caller also wraps. Callbacks fire on the LVGL task: post to a
 * queue and return.
 */
#ifndef NANO_UI_H
#define NANO_UI_H

#include <stdbool.h>

#include "lvgl.h"
#include "nano_decode.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    NANO_VIEW_MAIN = 0,
    NANO_VIEW_MENU,
    NANO_VIEW_SETTINGS,
    NANO_VIEW_TUNER,
    NANO_VIEW_TEMPO,
    NANO_VIEW_CONNECT,     /* no live state: "put the pedal in connect mode" / Connect button */
    NANO_VIEW_UPDATE,      /* firmware update over Wi-Fi (Bluetooth is off while it shows; closing restarts) */
    NANO_VIEW_PRESETS,     /* presets list, one bank per page: tap one to select it */
    NANO_VIEW_CAPTURE,     /* the capture: name, on / off, volume (tap the capture line) */
    NANO_VIEW_RENAME,      /* rename a preset (long press on its name): open with nano_ui_open_rename */
} nano_view_t;

/* What the firmware update view shows under the Wi-Fi line. */
typedef enum {
    NANO_UPDATE_BUSY = 0,     /* text = what is happening ("Connecting to ...", "Checking ...") */
    NANO_UPDATE_UP_TO_DATE,   /* text = the published version */
    NANO_UPDATE_AVAILABLE,    /* text = the new version: an Install button */
    NANO_UPDATE_DOWNLOADING,  /* percent */
    NANO_UPDATE_DONE,         /* installed, restarting */
    NANO_UPDATE_ERROR,        /* text = what went wrong: a Try again button */
} nano_update_state_t;

typedef struct {
    char ssid[33];
    int8_t rssi;
    bool secure;
} nano_ui_network_t;

typedef struct {
    void (*on_prev_preset)(void);
    void (*on_next_preset)(void);
    /* A preset was picked in the presets list (0..63). */
    void (*on_select_preset)(uint8_t index);
    /* Rename page: write `name` (1..NANO_PRESET_NAME_MAX chars) to preset `index` (0..63); answer with
     * nano_ui_rename_result. NULL = no long press opens the page. */
    void (*on_rename_preset)(uint8_t index, const char *name);
    /* Capture page: new capture volume, raw 0..255. NULL = the page shows the volume read-only. */
    void (*on_capture_volume)(uint8_t raw);
    /* A tile was tapped: slot 0..4 = pre1..post3, `on` = its state as shown. */
    void (*on_toggle_fx)(uint8_t slot, bool on);
    /* The GATE button was tapped; `on` = its state as shown. */
    void (*on_toggle_gate)(bool on);
    /* Tuner view opened (true) or closed (false): the app turns the pedal's tuner on / off. */
    void (*on_tuner)(bool on);
    /* The mute label in the tuner view was tapped: the app re-sends tuner-on with this mute flag. */
    void (*on_tuner_mute)(bool mute);
    /* Menu "Disconnect" / "Connect". */
    void (*on_link)(bool connect);
    /* Settings changed the presets-per-bank count (2..8). */
    void (*on_bank_size)(uint8_t per_bank);
    /* Settings changed the preset label style (nano_label_style_t). */
    void (*on_label_style)(uint8_t style);
    /* Settings toggled "Mute outputs 1/2": the app writes it to the pedal. */
    void (*on_outputs_mute)(bool mute);
    /* Settings toggled "Show expression pedal": the app persists it. */
    void (*on_expression_show)(bool show);
    /* Settings page 2: display turned 180 degrees (true) or not; the app rotates the panel and persists it. */
    void (*on_rotation)(bool rotate_180);
    /* Settings page 2: backlight brightness 1..10; the app applies and persists it. */
    void (*on_brightness)(uint8_t level);
    /* Tempo view: - / + pressed (delta in BPM). */
    void (*on_tempo_delta)(int delta);
    /* Tempo view opened (true) / closed (false) from the screen: the app puts the pedal in / out of tap tempo mode. */
    void (*on_tempo_view)(bool open);
    /* Settings "Check for updates": the app shuts Bluetooth down, starts Wi-Fi and checks. */
    void (*on_update_open)(void);
    /* The update view was closed: the app restarts (Bluetooth cannot come back without one). */
    void (*on_update_close)(void);
    /* Update view: list the Wi-Fi networks in range (answered with nano_ui_update_set_networks). */
    void (*on_wifi_scan)(void);
    /* A network was picked (password "" for an open one): join it, remember it, check. */
    void (*on_wifi_join)(const char *ssid, const char *password);
    void (*on_update_check)(void);
    void (*on_update_install)(void);
} nano_ui_callbacks_t;

void nano_ui_create(lv_display_t *disp, const nano_ui_callbacks_t *cb);
/* Status line and connection dot. */
void nano_ui_set_status(const char *text, bool connected);
/* Whether the link is wanted (drives the menu's Connect / Disconnect label). */
void nano_ui_set_link_enabled(bool enabled);
/* Presets per bank for the "3B" label (2..8). */
void nano_ui_set_bank_size(uint8_t per_bank);
/* Preset label style (nano_label_style_t). */
void nano_ui_set_label_style(uint8_t style);
/* Outputs 1/2 mute as the pedal reports it: top bar badge + settings toggle. */
void nano_ui_set_outputs_muted(bool muted);
/* Settings pager (for previews / tests). */
void nano_ui_settings_page(int index);
/* Display settings as loaded at boot (page 2 shows them). */
void nano_ui_set_rotation(bool rotate_180);
void nano_ui_set_brightness(uint8_t level);
/* Show a preset immediately (footswitch event / optimistic switch) using cached names. */
void nano_ui_set_preset(uint8_t index, const nano_metadata_t *meta);
/* Footswitch assignments IA, IB, IIA, IIB (preset indices): badges appear on the assigned preset. */
void nano_ui_set_footswitches(const uint8_t fs[4]);
/* Full refresh from a state dump plus cached metadata (meta may be NULL). */
void nano_ui_set_state(const nano_state_t *state, const nano_metadata_t *meta);
/* Tempo line: `tapping` = the pedal is in tap tempo mode (highlighted). 0 BPM clears it. */
void nano_ui_set_tempo(float bpm, bool tapping);
/* Grey everything out while there is no link. */
void nano_ui_set_stale(bool stale);
/*
 * live = a state dump arrived on this link: the main view becomes the base view (the connect page
 * closes if it is showing). false = the link is down: the connect page replaces the main, tuner
 * and tempo views (menu and settings stay open; closing them lands on the connect page).
 */
void nano_ui_set_connected(bool live);
/* Switch views (the tuner view calls on_tuner on open / close). */
void nano_ui_show(nano_view_t view);
/* Open the rename page for preset `index` (0..63). */
void nano_ui_open_rename(uint8_t index);
/* The pedal's answer to on_rename_preset: ok closes the page (the new name comes with the next
 * nano_ui_set_preset); otherwise `msg` shows and the name can be edited again. */
void nano_ui_rename_result(uint8_t index, bool ok, const char *msg);
nano_view_t nano_ui_view(void);
/* Tuner reading; `note` NULL = silence. */
void nano_ui_set_tuner(const char *note, float cents, bool in_tune);
/* The pedal's tuner started on the pedal itself: show the view without sending tuner-on. */
void nano_ui_open_tuner_from_pedal(void);
/* The pedal entered tap tempo mode itself: show the tempo view without sending anything. */
void nano_ui_open_tempo_from_pedal(void);
/* The pedal left a mode itself: back to the main view without sending anything. */
void nano_ui_close_from_pedal(void);
/*
 * Expression pedal. While the "Show expression pedal" setting is on, the side bar and the tracks on
 * the assigned FX tiles are always drawn: position 0..254 (heel..toe), -1 = unknown (drawn at the
 * heel). Assignments belong to the shown preset (NULL = none / unknown); values are what the pedal
 * produced for them (NULL = none yet, derived from the position).
 */
void nano_ui_set_expression_show(bool show);
void nano_ui_set_expression(int position);
void nano_ui_set_expression_assignments(const nano_exp_assignments_t *a);
void nano_ui_set_expression_values(const nano_exp_values_t *v);
/* Mute state as the pedal reports it (tuner report field 7). */
void nano_ui_set_tuner_mute(bool muted);
/* Running firmware version (settings page 3 and the update view). */
void nano_ui_set_firmware_version(const char *version);
/* Update view: the remembered / joined Wi-Fi network (NULL or "" = none yet). */
void nano_ui_update_set_wifi(const char *ssid);
void nano_ui_update_status(nano_update_state_t state, const char *text, int percent);
/* Update view: show the network picker ("Searching" while `scanning`, else the list). */
void nano_ui_update_show_networks(const nano_ui_network_t *networks, int count, bool scanning);

#ifdef __cplusplus
}
#endif
#endif
