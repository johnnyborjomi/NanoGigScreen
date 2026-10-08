/*
 * ui_text_edit: a one-line text field with an on-screen keyboard below it (private to nano_ui).
 *
 *   [ Gojira|            ]      <- text field, cursor at the end
 *   8 / 20                      <- status: length, or an error / "Saving..."
 *   q w e r t y u i o p  <-     <- LVGL keyboard down to the bottom of the screen
 *   ...                    OK
 *
 * What is being edited is described by ui_text_edit_cfg_t: the starting text, the length limits, an
 * optional check (e.g. a name already in use) and where a finished text goes. The preset name is one
 * user; another text the pedal stores is another cfg.
 *
 * Behaviour, the same for every text:
 * - OK (or Enter) checks the length and cfg.validate, then hands the text to cfg.on_submit and shows
 *   "Saving..." with the keyboard locked until the page reports ui_text_edit_set_error() or closes.
 * - A text that equals the starting one is handed over too; the page decides (usually: just close).
 *
 * Memory: freed with its container (LV_EVENT_DELETE), so a page built on open costs nothing closed.
 */
#ifndef UI_TEXT_EDIT_H
#define UI_TEXT_EDIT_H

#include <stdbool.h>
#include <stddef.h>

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *text;                                   /* starting text (copied) */
    const char *placeholder;
    int min_len, max_len;                               /* bytes; max_len also stops the keyboard */
    /* Optional: false + a short reason (shown in the status line) to refuse a text. */
    bool (*validate)(const char *text, char *why, size_t cap, void *user);
    void (*on_submit)(const char *text, void *user);    /* a text that passed the checks */
    void *user;
} ui_text_edit_cfg_t;

typedef struct ui_text_edit ui_text_edit_t;

/* Build across `parent` from `y` to the bottom of the screen. `cfg` is copied. */
ui_text_edit_t *ui_text_edit_create(lv_obj_t *parent, int32_t y, const ui_text_edit_cfg_t *cfg);

/* The submitted text was refused (or not answered): show `msg` and unlock the keyboard. */
void ui_text_edit_set_error(ui_text_edit_t *t, const char *msg);

/* The text in the field. */
const char *ui_text_edit_text(const ui_text_edit_t *t);

#ifdef __cplusplus
}
#endif
#endif
