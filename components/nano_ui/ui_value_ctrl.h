/*
 * ui_value_ctrl: a full-width control for one pedal parameter (private to nano_ui).
 *
 *     +2.1 dB              <- value; double tap = reset (optional)
 *   =========O-------      <- slider
 *   [-1 dB] [-0.1]   [+0.1] [+1 dB]
 *
 * The parameter itself is described by ui_value_ctrl_cfg_t: its raw range (what the pedal stores),
 * how raw maps to the slider and to the readout, the step sizes and where changes go. The capture
 * volume is one user; another parameter is another cfg.
 *
 * Behaviour, the same for every parameter:
 * - Fine steps move to the next raw value whose readout differs (the smallest change the pedal can
 *   make). Coarse steps aim at exact readouts across a run of taps, each landing on the closest one.
 * - The slider follows the finger in slider units, ignores a resting finger's back-and-forth
 *   (cfg.turn), writes at most every cfg.drag_ms while dragged and once more on release.
 * - For hold_ms after a change made here, reports from the pedal for the same owner (a preset) are
 *   ignored: they were requested before the change and would make the value jump back.
 *
 * Memory: the control lives in a container object and is freed with it (LV_EVENT_DELETE), so a page
 * built on open and deleted on close costs nothing while closed.
 */
#ifndef UI_VALUE_CTRL_H
#define UI_VALUE_CTRL_H

#include <stdbool.h>
#include <stddef.h>

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int raw_min, raw_max;                          /* what the pedal stores */
    int pos_min, pos_max;                          /* slider units (e.g. tenths of a dB) */
    int (*raw_to_pos)(int raw);
    int (*pos_to_raw)(int pos);
    int (*readout)(int raw);                       /* shown value in step units, non-decreasing in raw */
    void (*format)(int raw, char *out, size_t cap); /* "+2.1 dB" */
    int fine, coarse;                              /* step sizes in readout units (1 and 10 for 0.1 / 1 dB) */
    const char *step_labels[4];                    /* left to right: -coarse, -fine, +fine, +coarse (static strings) */
    int reset_raw;                                 /* double tap on the value goes here; -1 = no reset */
    int turn;                                      /* slider units a resting finger may wobble back; 0 = 4 */
    int drag_ms;                                   /* write interval while dragging; 0 = 100 */
    int hold_ms;                                   /* ignore the pedal's reports this long after a change; 0 = 1500 */
    void (*on_change)(int raw, void *user);         /* a new value to write; NULL = read-only */
    void *user;
} ui_value_ctrl_cfg_t;

typedef struct ui_value_ctrl ui_value_ctrl_t;

/* Build the control across `parent` (its width) from `y` down (UI_VALUE_CTRL_HEIGHT tall). `cfg` is copied. */
#define UI_VALUE_CTRL_HEIGHT 130
ui_value_ctrl_t *ui_value_ctrl_create(lv_obj_t *parent, int32_t y, const ui_value_ctrl_cfg_t *cfg);

/* The value as the pedal reports it (-1 = unknown: shown as "-", controls off). `owner` identifies
 * what it belongs to (the preset): a report for a different owner always shows at once. */
void ui_value_ctrl_report(ui_value_ctrl_t *c, int raw, int owner);

/* Like ui_value_ctrl_report, but shown even while a change made here holds (the pedal's answer to a read
 * made after that change). Leaves a finger on the slider alone. */
void ui_value_ctrl_set(ui_value_ctrl_t *c, int raw, int owner);

/* The value shown (-1 = unknown). */
int ui_value_ctrl_value(const ui_value_ctrl_t *c);

#ifdef __cplusplus
}
#endif
#endif
