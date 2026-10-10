/*
 * nano_ui's insides, shared by its page files (private to nano_ui).
 *
 * One page per file (page_*.c). A page is a ui_page_t: how to build its root, forget its widgets when the root
 * is deleted, and what happens on enter / leave. nano_ui.c switches pages generically (ui_show_view): pages are
 * built on open and deleted on close, except the ones marked `keep`. Each page keeps its widgets in one static
 * struct that `destroy` clears, so nothing points into a deleted page; its public setters store the value and
 * redraw only while the page exists (the build draws from the stored values).
 *
 * Values more than one page shows live in g_ui.
 */
#ifndef UI_INTERNAL_H
#define UI_INTERNAL_H

#include <stdbool.h>
#include <stdint.h>

#include "nano_ui.h"
#include "ui_common.h"

/* ---- layout shared by several pages ---------------------------------------- */

#define TOP_H 22                 /* title bar */
#define EDGE_X 2                 /* left edge shared by the prev button, the gate button, the first tile, the pager */
#define EXP_BAR_W 3              /* the expression bar in the main view's right gutter */
#define EXP_BAR_X (SCREEN_W - EXP_BAR_W)
#define RIGHT_X (EXP_BAR_X - 1)  /* right edge of the next button and the last tile */
#define PAGER_W 36
#define SETTING_X (EDGE_X + PAGER_W + 8) /* page content right of the pager column */
#define SETTING_RIGHT (SCREEN_W - 12 - SETTING_X)
#define SETTING_ROW_H 34

/* Slot colours for the preset label (1A red, 1B orange, 1C green, 1D cyan ...). */
extern const uint32_t UI_SLOT_COLORS[8];

/* ---- shared state ------------------------------------------------------------- */

typedef struct {
    nano_ui_callbacks_t cb;
    lv_obj_t *scr;
    nano_view_t view;
    nano_view_t base_view;       /* where "close" lands: the main view once a state arrived, else the connect page */
    uint8_t preset;              /* the shown preset */
    const nano_metadata_t *meta; /* the app's cache, as last passed with a preset (NULL = none) */
    uint8_t per_bank;
    nano_label_style_t label_style;
    bool link_enabled;
    bool outputs_muted;          /* main view badge, settings switch */
    bool exp_show;               /* main view indicators, settings switch */
    float tempo_bpm;             /* main view line, tempo page */
    bool tempo_tapping;
    char fw_version[32];         /* settings page 3, update page */
    char wifi_ssid[33];          /* update page */
} ui_t;

extern ui_t g_ui;

/* ---- pages -------------------------------------------------------------------- */

typedef struct {
    lv_obj_t *(*build)(lv_obj_t *scr);  /* the root (hidden; shown by ui_show_view) */
    void (*destroy)(void);              /* the root is being deleted: forget its widgets */
    /* Shown as `view` (a page may stand for two views: the Capture / IR tabs), coming from `from`. `notify` = the
     * user asked (tell the app), not the pedal. */
    void (*enter)(nano_view_t view, nano_view_t from, bool notify);
    void (*leave)(nano_view_t to, bool notify);
    bool keep;       /* built once, kept hidden while another page shows */
    bool underlay;   /* never hidden: the overlays cover it (the gig view) */
    bool needs_link; /* the connect page replaces it when the link drops */
    lv_obj_t *root;
} ui_page_t;

extern ui_page_t page_main, page_menu, page_settings, page_tuner, page_tempo, page_connect, page_update, page_presets,
    page_source, page_rename, page_ir_list, page_fx, page_gate;

void ui_show_view(nano_view_t view, bool notify);
void ui_go(nano_view_t view);   /* the user's way: nano_ui_show */
void ui_go_base(void);          /* close: the main view, or the connect page while there is no state */
bool ui_page_built(const ui_page_t *page);

/* "Preset 12" for a preset without a cached name. Returns `fallback` or the cached name. */
const char *ui_preset_name(uint8_t index, char *fallback, size_t cap);

/* Cross-page refreshes (each a no-op while its page is not built). */
void main_refresh_expression(void);
void main_refresh_mute(void);
void main_refresh_tempo(void);
void settings_refresh(void);
void menu_refresh_link(void);
void connect_refresh_link(void);
void connect_set_status(const char *text, bool connected);
void tempo_page_refresh(void);
void presets_refresh(void);
void presets_forget_return_bank(void);
void source_from_state(const nano_state_t *st); /* Capture / IR tabs: the shown state's capture and IR */
void ir_tab_from_state(const nano_state_t *st);
void capture_tab_refresh(void);
void ir_tab_refresh(bool fresh);
const char *ir_tab_name(void);             /* the shown IR's name ("" = none), on or off */
bool ir_is_saved(const char *name);        /* the shown preset was saved with this IR (shown in C_SAVED) */
const char *capture_tab_name(void);        /* the same for the capture */
bool capture_is_saved(const char *name);
void ir_list_refresh(void);                /* the IR list: the current IR changed */
void fx_from_state(const nano_state_t *st); /* the FX editor: the blocks as the state shows them */
void gate_from_state(const nano_state_t *st); /* the gate page */

#endif
