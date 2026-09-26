/*
 * The gig screen (LVGL 9, 320x240) and its overlays (menu, settings, tuner).
 *
 * Main view: top bar (link status, tempo, gate, menu button), preset row
 * (prev button, bank label + name, next button), capture and IR lines, five
 * FX tiles in category colours. Tap a tile to toggle that block.
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
} nano_view_t;

typedef struct {
    void (*on_prev_preset)(void);
    void (*on_next_preset)(void);
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
    /* Tempo view: - / + pressed (delta in BPM). */
    void (*on_tempo_delta)(int delta);
    /* Tempo view opened (true) / closed (false) from the screen: the app puts the pedal in / out of tap tempo mode. */
    void (*on_tempo_view)(bool open);
} nano_ui_callbacks_t;

void nano_ui_create(lv_display_t *disp, const nano_ui_callbacks_t *cb);
/* Status line and connection dot. */
void nano_ui_set_status(const char *text, bool connected);
/* Whether the link is wanted (drives the menu's Connect / Disconnect label). */
void nano_ui_set_link_enabled(bool enabled);
/* Presets per bank for the "3B" label (2..8). */
void nano_ui_set_bank_size(uint8_t per_bank);
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
/* Switch views (the tuner view calls on_tuner on open / close). */
void nano_ui_show(nano_view_t view);
nano_view_t nano_ui_view(void);
/* Tuner reading; `note` NULL = silence. */
void nano_ui_set_tuner(const char *note, float cents, bool in_tune);
/* The pedal's tuner started on the pedal itself: show the view without sending tuner-on. */
void nano_ui_open_tuner_from_pedal(void);
/* The pedal entered tap tempo mode itself: show the tempo view without sending anything. */
void nano_ui_open_tempo_from_pedal(void);
/* The pedal left a mode itself: back to the main view without sending anything. */
void nano_ui_close_from_pedal(void);
/* Mute state as the pedal reports it (tuner report field 7). */
void nano_ui_set_tuner_mute(bool muted);

#ifdef __cplusplus
}
#endif
#endif
