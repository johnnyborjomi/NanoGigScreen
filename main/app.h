/*
 * What the app's modules share. Everything here runs on the app task (main.c): the modules call each other
 * directly, nothing in them locks. Two rules hold it together:
 *   - the pedal's state lives in g_app; a module that changes what the screen should show marks a screen part
 *     (ui_mark) and pushes it from its own state in its *_ui_push, under the LVGL lock taken by ui_sync;
 *   - every frame goes out through app_send.
 * Each feature module has the same small interface where it needs one: *_tick (deadlines, every loop),
 * *_link_reset (the link dropped: drop in-flight work), *_ui_push (its screen parts).
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "nano_build.h"
#include "nano_decode.h"

/* ---- shared state ----------------------------------------------------------- */

typedef struct {
    bool link_ready;          /* subscribed to the pedal's notifications */
    nano_state_t state;       /* the pedal's state as shown: the last dump, with our own writes applied */
    bool state_valid;         /* a dump arrived since start (kept across a drop: the screen greys it out) */
    nano_metadata_t *meta;    /* preset / capture / IR names, cached in flash (settings.c owns the memory) */
    bool meta_valid;
} app_t;

extern app_t g_app;

static inline const nano_metadata_t *app_meta(void) { return g_app.meta_valid ? g_app.meta : NULL; }

int64_t app_now_us(void);

/*
 * Send one frame to the pedal: `n` is what the builder returned (0 = it did not build: logged, false). True when
 * the write was queued. Typical use:
 *     uint8_t f[NANO_FRAME_MAX];
 *     if (app_send(f, nano_build_x(f, sizeof(f), ...))) ESP_LOGI(TAG, "-> x");
 */
bool app_send(const uint8_t *frame, size_t n);

/* State re-reads (link.c). */
#define CONFIRM_MS 300     /* after our own write: one dump confirms it */
#define DEBOUNCE_MS 400    /* after a pedal change notice */
void link_schedule_state(uint32_t delay_ms);

/* ---- screen sync ------------------------------------------------------------- */

/*
 * Screen parts. Code that changes what a part shows marks it (ui_mark); ui_sync() pushes every marked part under one
 * LVGL lock, now or, while the display is busy, on the next loop: a busy display delays an update, never loses it.
 * Each part is pushed from its module's current state, so the latest value wins. Live readings (tuner pitch,
 * expression values) go straight to the screen and may skip a frame.
 */
enum {
    UI_LINK = 1u << 0,        /* status line, greyed out, connect page on a drop (link.c; pushed last) */
    UI_STATE = 1u << 1,       /* the gig view from g_app.state */
    UI_SYNCED = 1u << 2,      /* a state of this link is on screen: un-grey, close the connect page */
    UI_PRESET = 1u << 3,      /* preset label and name only (select, label style, bank size, rename) */
    UI_FOOTSWITCHES = 1u << 4,
    UI_OUTPUTS = 1u << 5,     /* outputs 1/2 muted badge (link.c) */
    UI_EXP_POS = 1u << 6,     /* expression position (expression.c) */
    UI_EXP_CLEAR = 1u << 7,   /* expression values unknown */
    UI_EXP_ASSIGN = 1u << 8,  /* expression targets of the shown preset */
    UI_PAGES = 1u << 9,       /* remote pages with a new answer (remote_page.c) */
    UI_RENAME = 1u << 10,     /* the rename page's answer (rename.c) */
    UI_TUNER_MUTE = 1u << 11, /* tuner.c */
    UI_ROTATION = 1u << 12,   /* settings.c */
    UI_LIBRARY = 1u << 13,    /* the capture / IR list page's list (library.c) */
    UI_IR_FOLDERS = 1u << 14, /* the IR list page's folders (ir_folder_store.c) */
};
/* Parts of the old link that must not reach the greyed-out screen after a drop. */
#define UI_PARTS_OF_LINK (UI_STATE | UI_SYNCED | UI_PRESET | UI_FOOTSWITCHES | UI_EXP_ASSIGN)

#define UI_LOCK_MS 30         /* ui_sync waits this long for the display; a miss is retried next loop */
#define VIEW_LOCK_MS 300      /* the pedal opens / closes a view (tuner, tap tempo): rare, must not be missed */

void ui_mark(uint32_t parts);
void ui_unmark(uint32_t parts); /* superseded before it was shown */
void ui_sync(void);
