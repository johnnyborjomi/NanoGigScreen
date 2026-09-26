/*
 * The gig screen (LVGL 9, 320x240): preset label + name in big type, capture
 * and IR names with on/off dots, five FX tiles in category colours, a status
 * line. Tap the left / right edge of the preset name to switch presets, a tile to
 * toggle that block.
 *
 * Every call must hold the LVGL lock (lvgl_port_lock) except nano_ui_create,
 * which the caller also wraps.
 */
#ifndef NANO_UI_H
#define NANO_UI_H

#include <stdbool.h>

#include "lvgl.h"
#include "nano_decode.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    void (*on_prev_preset)(void);
    void (*on_next_preset)(void);
    /* A tile was tapped: slot 0..4 = pre1..post3, `on` = its state as shown. */
    void (*on_toggle_fx)(uint8_t slot, bool on);
} nano_ui_callbacks_t;

void nano_ui_create(lv_display_t *disp, const nano_ui_callbacks_t *cb);
/* Status line (bottom-left of the top bar) and connection dot colour. */
void nano_ui_set_status(const char *text, bool connected);
/* Show a preset immediately (footswitch event / optimistic switch) using cached names. */
void nano_ui_set_preset(uint8_t index, const nano_metadata_t *meta);
/* Full refresh from a state dump plus cached metadata (meta may be NULL). */
void nano_ui_set_state(const nano_state_t *state, const nano_metadata_t *meta);
/* Grey everything out while there is no link. */
void nano_ui_set_stale(bool stale);

#ifdef __cplusplus
}
#endif
#endif
