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
    /* Menu "Disconnect" / "Connect". */
    void (*on_link)(bool connect);
    /* Settings changed the presets-per-bank count (2..8). */
    void (*on_bank_size)(uint8_t per_bank);
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
/* Full refresh from a state dump plus cached metadata (meta may be NULL). */
void nano_ui_set_state(const nano_state_t *state, const nano_metadata_t *meta);
/* Grey everything out while there is no link. */
void nano_ui_set_stale(bool stale);
/* Switch views (the tuner view calls on_tuner on open / close). */
void nano_ui_show(nano_view_t view);
nano_view_t nano_ui_view(void);
/* Tuner reading; `note` NULL = silence. */
void nano_ui_set_tuner(const char *note, float cents, bool in_tune);

#ifdef __cplusplus
}
#endif
#endif
