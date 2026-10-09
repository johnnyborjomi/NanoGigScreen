/*
 * IR list: every IR on the pedal, as Cortex Cloud's IR loader lists them (not just the five on the pedal's IR list),
 * in folders of our own (nano_ir_folders.h). Tabs User / Factory in the header, five rows a page:
 *   - the top of a list: its folders, then the IRs in none; inside a folder: its IRs;
 *   - tap an IR to load it into the preset (a live edit; the list stays open to try the next), tap a folder to open
 *     it, hold an IR to move it (the rows become the folders to put it in);
 *   - the bar at the bottom: "+ Folder" at the top of a list; "< name", rename and delete (tap twice) in a folder.
 * The current IR has a green outline, the preset's saved one teal text. The app reads the library on open
 * (nano_ui_set_ir_library) and owns the folders (nano_ui_set_ir_folders): both are copies freed on close. Built on
 * open; "<" goes back to the IR tab.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ui_internal.h"
#include "ui_text_edit.h"
#include "ui_widgets.h"

#define ROWS 5
#define ROW_GAP 4
#define BAR_H 32
#define ICON_W 56
#define ARM_MS 3000                 /* delete: the second tap within this */

static const int LISTS[2] = { NANO_IR_USER, NANO_IR_FACTORY }; /* tab order */

typedef enum { MODE_BROWSE, MODE_MOVE, MODE_NAME_NEW, MODE_RENAME } view_mode_t;
typedef enum { ITEM_FOLDER, ITEM_IR, ITEM_TOP } item_kind_t;
typedef struct {
    item_kind_t kind;
    int index;                      /* ITEM_FOLDER: into folders[]; ITEM_IR: in the list */
} item_t;

static struct {
    nano_ir_library_t *lib;         /* owned; NULL while the pedal has not answered */
    nano_ir_folders_t *folders;     /* owned copy; NULL until the app sent it */
    int tab;                        /* 0 = User, 1 = Factory */
    uint8_t folder;                 /* open folder, NANO_IR_NO_FOLDER = the top of the list */
    view_mode_t mode;
    char moving[NANO_CAB_NAME_CAP]; /* MODE_MOVE: the IR */
    uint32_t armed_ms;              /* delete armed at (0 = not) */
} s;

static struct {
    bool built;
    lv_obj_t *tabs[2], *rows[ROWS], *names[ROWS], *note;
    lv_obj_t *bar_main, *bar_rename, *bar_delete, *bar_text, *naming;
    lv_timer_t *disarm;
    ui_pager_t pager;
} w;

static int list(void) { return LISTS[s.tab]; }
static const nano_ir_folder_t *open_folder(void) { return s.folders ? nano_ir_folder_find(s.folders, s.folder) : NULL; }
static uint8_t folder_of(const char *ir) { return s.folders ? nano_ir_folder_of(s.folders, list(), ir) : NANO_IR_NO_FOLDER; }
static void reload(int page);

/* ---- items: what the rows show in this mode ------------------------------------------- */

/* Item `n` of the current view; false past the end. */
static bool item_at(int n, item_t *it)
{
    int folders = s.folders ? s.folders->count : 0;
    if (s.mode == MODE_MOVE) {
        if (folder_of(s.moving) != NANO_IR_NO_FOLDER && n-- == 0) {
            *it = (item_t){ ITEM_TOP, 0 };
            return true;
        }
    }
    if (s.mode == MODE_MOVE || s.folder == NANO_IR_NO_FOLDER) {
        for (int i = 0; i < folders; i++) {
            if (s.folders->folders[i].list == list() && n-- == 0) {
                *it = (item_t){ ITEM_FOLDER, i };
                return true;
            }
        }
        if (s.mode == MODE_MOVE) return false;
    }
    for (int i = 0; s.lib && i < s.lib->count[list()]; i++) {
        const char *name = nano_ir_library_name(s.lib, list(), i);
        if (name[0] && folder_of(name) == s.folder && n-- == 0) {
            *it = (item_t){ ITEM_IR, i };
            return true;
        }
    }
    return false;
}

static int item_count(void)
{
    item_t it;
    int n = 0;
    while (item_at(n, &it)) n++;
    return n;
}

static int folder_size(uint8_t id)
{
    int n = 0;
    for (int i = 0; s.folders && i < s.folders->filed_count; i++) n += s.folders->filed_folder[i] == id;
    return n;
}

/* ---- the bottom bar ------------------------------------------------------------------- */

static void disarm(void)
{
    s.armed_ms = 0;
    if (w.disarm) lv_timer_pause(w.disarm);
    lv_obj_set_style_bg_color(w.bar_delete, lv_color_hex(C_PANEL), 0);
    lv_label_set_text(lv_obj_get_child(w.bar_delete, 0), LV_SYMBOL_TRASH);
}

static void on_disarm_timer(lv_timer_t *t) { (void)t; disarm(); }

static void show_bar(void)
{
    const nano_ir_folder_t *d = open_folder();
    bool browse = s.mode == MODE_BROWSE, in_folder = browse && d;
    char t[NANO_IR_FOLDER_NAME_MAX + 8];
    if (s.mode == MODE_MOVE) snprintf(t, sizeof(t), LV_SYMBOL_CLOSE " Cancel");
    else if (in_folder) snprintf(t, sizeof(t), LV_SYMBOL_LEFT " %s", d->name);
    else snprintf(t, sizeof(t), LV_SYMBOL_PLUS " Folder");
    lv_label_set_text(lv_obj_get_child(w.bar_main, 0), t);
    lv_obj_set_hidden(w.bar_main, s.mode == MODE_NAME_NEW || s.mode == MODE_RENAME);
    lv_obj_set_hidden(w.bar_rename, !in_folder);
    lv_obj_set_hidden(w.bar_delete, !in_folder);
    lv_obj_set_hidden(w.bar_text, in_folder || s.mode == MODE_NAME_NEW || s.mode == MODE_RENAME);
    if (s.mode == MODE_MOVE) lv_label_set_text_fmt(w.bar_text, "Move %s to:", s.moving);
    else lv_label_set_text(w.bar_text, s.lib && s.folders ? "Hold an IR to move it" : "");
    if (!in_folder) disarm();
}

/* ---- rows ------------------------------------------------------------------------------ */

static void show_page(int page)
{
    const char *current = ir_tab_name();
    for (int i = 0; i < ROWS; i++) {
        item_t it;
        bool shown = s.mode != MODE_NAME_NEW && s.mode != MODE_RENAME && item_at(page * ROWS + i, &it);
        lv_obj_set_hidden(w.rows[i], !shown);
        if (!shown) continue;
        uint32_t color = C_TEXT;
        bool outlined = false;
        if (it.kind == ITEM_IR) {
            const char *name = nano_ir_library_name(s.lib, list(), it.index);
            lv_label_set_text(w.names[i], name);
            outlined = strcmp(name, current) == 0;
            if (ir_is_saved(name)) color = C_SAVED;
        } else if (it.kind == ITEM_FOLDER) {
            const nano_ir_folder_t *d = &s.folders->folders[it.index];
            lv_label_set_text_fmt(w.names[i], LV_SYMBOL_DIRECTORY "  %s  (%d)", d->name, folder_size(d->id));
            color = C_ACCENT;
            outlined = s.mode == MODE_MOVE && folder_of(s.moving) == d->id;
        } else {
            lv_label_set_text(w.names[i], LV_SYMBOL_UP "  Top of the list");
            color = C_MUTED;
        }
        lv_obj_set_style_text_color(w.names[i], lv_color_hex(color), 0);
        lv_obj_set_style_border_width(w.rows[i], outlined ? 2 : 0, 0);
    }
    const char *note = !s.lib ? "Reading the pedal's IRs..."
                       : s.mode == MODE_MOVE && !item_count() ? "No folders yet: make one with + Folder"
                       : !item_count() && s.mode == MODE_BROWSE ? (s.folder ? "Empty: hold an IR to move it here" : s.tab == 0 ? "No user IRs" : "No factory IRs")
                       : "";
    lv_label_set_text(w.note, note);
    show_bar();
}

static void reload(int page)
{
    int pages = (item_count() + ROWS - 1) / ROWS;
    ui_pager_set_count(&w.pager, pages ? pages : 1, page);
}

/* The page of the current IR in this view, else the first. */
static int page_of_current(void)
{
    const char *current = ir_tab_name();
    item_t it;
    for (int n = 0; item_at(n, &it); n++) {
        if (it.kind == ITEM_IR && strcmp(nano_ir_library_name(s.lib, list(), it.index), current) == 0) return n / ROWS;
    }
    return 0;
}

static void edit(nano_ir_folder_op_t op, uint8_t folder, const char *text)
{
    if (g_ui.cb.on_ir_folder_edit) g_ui.cb.on_ir_folder_edit((uint8_t)op, (uint8_t)list(), folder, text);
}

/* ---- naming a folder: the keyboard over the rows ------------------------------------------ */

static void end_naming(void)
{
    if (w.naming) lv_obj_delete_async(w.naming);
    w.naming = NULL;
    s.mode = MODE_BROWSE;
    reload(0); /* the pager shows its column again if it needs one */
}

static bool validate(const char *text, char *why, size_t cap, void *user)
{
    (void)user;
    if (text[0] == ' ' || text[strlen(text) - 1] == ' ') {
        snprintf(why, cap, "No space at the start or end");
        return false;
    }
    if (s.folders && nano_ir_folder_named(s.folders, list(), text, s.mode == MODE_RENAME ? s.folder : NANO_IR_NO_FOLDER)) {
        snprintf(why, cap, "A folder has this name");
        return false;
    }
    if (s.mode == MODE_NAME_NEW && s.folders && s.folders->count >= NANO_IR_FOLDERS_MAX) {
        snprintf(why, cap, "%d folders is the most", NANO_IR_FOLDERS_MAX);
        return false;
    }
    return true;
}

static void submit(const char *text, void *user)
{
    (void)user;
    if (s.mode == MODE_NAME_NEW) edit(NANO_IR_FOLDER_CREATE, NANO_IR_NO_FOLDER, text);
    else if (strcmp(text, open_folder() ? open_folder()->name : "") != 0) edit(NANO_IR_FOLDER_RENAME, s.folder, text);
    end_naming();
}

static void start_naming(view_mode_t mode)
{
    s.mode = mode;
    show_page(0); /* rows and bar away */
    lv_obj_set_hidden(w.pager.up, true);
    lv_obj_set_hidden(w.pager.down, true);
    lv_obj_set_hidden(w.pager.label, true);
    lv_obj_t *root = lv_obj_get_parent(w.rows[0]);
    w.naming = ui_box(root, 0, 0, SCREEN_W, SCREEN_H, C_BG);
    lv_obj_set_style_bg_opa(w.naming, LV_OPA_TRANSP, 0);
    lv_obj_set_clickable(w.naming, false);
    const ui_text_edit_cfg_t cfg = {
        .text = mode == MODE_RENAME && open_folder() ? open_folder()->name : "",
        .placeholder = "Folder name",
        .min_len = 1, .max_len = NANO_IR_FOLDER_NAME_MAX,
        .validate = validate,
        .on_submit = submit,
    };
    ui_text_edit_create(w.naming, TOP_H + 8, &cfg);
}

/* ---- events ------------------------------------------------------------------------------ */

static void on_back(lv_event_t *e)
{
    (void)e;
    if (s.mode == MODE_NAME_NEW || s.mode == MODE_RENAME) end_naming();
    else ui_go(NANO_VIEW_IR);
}

static void on_close(lv_event_t *e) { (void)e; ui_go_base(); }

static void select_tab(int tab)
{
    if (w.naming) {
        lv_obj_delete_async(w.naming);
        w.naming = NULL;
    }
    s.tab = tab;
    s.mode = MODE_BROWSE;
    ui_header_tabs_select(w.tabs, tab);
    s.folder = folder_of(ir_tab_name()); /* where the current IR is */
    reload(page_of_current());
}

static void on_tab(lv_event_t *e) { select_tab((int)(intptr_t)lv_event_get_user_data(e)); }

static void on_row(lv_event_t *e)
{
    item_t it;
    if (!item_at(w.pager.current * ROWS + (int)(intptr_t)lv_event_get_user_data(e), &it)) return;
    if (s.mode == MODE_MOVE) {
        edit(NANO_IR_FOLDER_FILE, it.kind == ITEM_FOLDER ? s.folders->folders[it.index].id : NANO_IR_NO_FOLDER, s.moving);
        s.mode = MODE_BROWSE;
        reload(w.pager.current);
    } else if (it.kind == ITEM_FOLDER) {
        s.folder = s.folders->folders[it.index].id;
        reload(0);
    } else if (it.kind == ITEM_IR && g_ui.cb.on_ir_pick) {
        g_ui.cb.on_ir_pick((uint8_t)list(), (uint16_t)it.index, nano_ir_library_name(s.lib, list(), it.index));
    }
}

static void on_row_long(lv_event_t *e)
{
    item_t it;
    if (s.mode != MODE_BROWSE || !s.folders || !item_at(w.pager.current * ROWS + (int)(intptr_t)lv_event_get_user_data(e), &it) || it.kind != ITEM_IR) return;
    snprintf(s.moving, sizeof(s.moving), "%s", nano_ir_library_name(s.lib, list(), it.index));
    s.mode = MODE_MOVE;
    reload(0);
}

/* "+ Folder" / "< name" (up) / "x Cancel". */
static void on_bar_main(lv_event_t *e)
{
    (void)e;
    if (s.mode == MODE_MOVE) {
        s.mode = MODE_BROWSE;
        reload(0);
    } else if (open_folder()) {
        s.folder = NANO_IR_NO_FOLDER;
        reload(0);
    } else if (s.folders) {
        start_naming(MODE_NAME_NEW);
    }
}

static void on_bar_rename(lv_event_t *e) { (void)e; if (open_folder()) start_naming(MODE_RENAME); }

static void on_bar_delete(lv_event_t *e)
{
    (void)e;
    if (!open_folder()) return;
    if (s.armed_ms && lv_tick_elaps(s.armed_ms) < ARM_MS) {
        edit(NANO_IR_FOLDER_DELETE, s.folder, "");
        s.folder = NANO_IR_NO_FOLDER;
        disarm();
        return; /* the app's answer reloads */
    }
    s.armed_ms = lv_tick_get() | 1;
    lv_obj_set_style_bg_color(w.bar_delete, lv_color_hex(C_ERROR), 0);
    lv_label_set_text(lv_obj_get_child(w.bar_delete, 0), "Sure?");
    lv_timer_reset(w.disarm);
    lv_timer_resume(w.disarm);
}

/* ---- page ---------------------------------------------------------------------------------- */

static lv_obj_t *bar_button(lv_obj_t *root, int32_t x, int32_t width, const char *text, lv_event_cb_t cb)
{
    lv_obj_t *b = ui_button(root, x, SCREEN_H - 4 - BAR_H, width, BAR_H, text, &lv_font_montserrat_14, C_PANEL, C_TEXT, cb, NULL);
    lv_obj_set_style_radius(b, 8, 0);
    return b;
}

static lv_obj_t *build(lv_obj_t *scr)
{
    ui_overlay_t o = ui_overlay(scr, "", on_back, on_close);
    static const char *const NAMES[2] = { "User", "Factory" };
    ui_header_tabs(o.root, NAMES, on_tab, w.tabs);
    const int32_t top = TOP_H + 6, h = SCREEN_H - top - 4 - BAR_H - 6, row_h = (h - (ROWS - 1) * ROW_GAP) / ROWS;
    ui_pager_create(&w.pager, o.root, top, h, "Page");
    w.pager.on_show = show_page;
    w.pager.wrap = true;
    for (int i = 0; i < ROWS; i++) {
        lv_obj_t *row = ui_box(o.root, SETTING_X, top + i * (row_h + ROW_GAP), SETTING_RIGHT, row_h, C_PANEL);
        lv_obj_set_style_radius(row, 8, 0);
        lv_obj_set_style_border_color(row, lv_color_hex(C_ON), 0);
        lv_obj_set_clickable(row, true);
        lv_obj_set_style_bg_color(row, lv_color_hex(C_TEXT), LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(row, LV_OPA_30, LV_STATE_PRESSED);
        lv_obj_add_event_cb(row, on_row, LV_EVENT_SHORT_CLICKED, (void *)(intptr_t)i);
        lv_obj_add_event_cb(row, on_row_long, LV_EVENT_LONG_PRESSED, (void *)(intptr_t)i);
        w.names[i] = ui_line_label(row, 8, 0, SETTING_RIGHT - 16, &lv_font_montserrat_14, C_TEXT);
        lv_obj_align(w.names[i], LV_ALIGN_LEFT_MID, 8, 0);
        lv_obj_set_hidden(row, true);
        w.rows[i] = row;
    }
    w.note = ui_label(o.root, &lv_font_montserrat_14, C_MUTED);
    lv_obj_set_pos(w.note, SETTING_X + 8, top + 8);
    /* Bottom bar: [main] ... [rename] [delete], or [main] and a line of text. */
    const int32_t right = SCREEN_W - EDGE_X - 2;
    w.bar_main = bar_button(o.root, EDGE_X, 150, "", on_bar_main);
    lv_obj_t *l = lv_obj_get_child(w.bar_main, 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    lv_obj_set_width(l, 150 - 12);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(l);
    w.bar_rename = bar_button(o.root, right - 2 * ICON_W - 6, ICON_W, LV_SYMBOL_EDIT, on_bar_rename);
    w.bar_delete = bar_button(o.root, right - ICON_W, ICON_W, LV_SYMBOL_TRASH, on_bar_delete);
    w.bar_text = ui_line_label(o.root, EDGE_X + 150 + 8, 0, right - EDGE_X - 150 - 8, &lv_font_montserrat_12, C_MUTED);
    lv_obj_set_y(w.bar_text, SCREEN_H - 4 - BAR_H + (BAR_H - lv_font_get_line_height(&lv_font_montserrat_12)) / 2);
    w.disarm = lv_timer_create(on_disarm_timer, ARM_MS, NULL);
    lv_timer_pause(w.disarm);
    w.built = true;
    return o.root;
}

static void destroy(void)
{
    w.built = false;
    w.naming = NULL;
    if (w.disarm) lv_timer_delete(w.disarm);
    w.disarm = NULL;
    free(s.lib);
    free(s.folders);
    s.lib = NULL;
    s.folders = NULL;
}

static void enter(nano_view_t view, nano_view_t from, bool notify)
{
    (void)view, (void)from, (void)notify;
    s.armed_ms = 0;
    select_tab(0);
    if (g_ui.cb.on_ir_library) g_ui.cb.on_ir_library(true);
}

static void leave(nano_view_t to, bool notify)
{
    (void)to, (void)notify;
    if (g_ui.cb.on_ir_library) g_ui.cb.on_ir_library(false);
}

ui_page_t page_ir_list = { .build = build, .destroy = destroy, .enter = enter, .leave = leave, .needs_link = true };

void ir_list_refresh(void)
{
    if (w.built && !w.naming) show_page(w.pager.current);
}

/* Opens on the tab holding the current IR (User when it is in neither), in its folder. */
void nano_ui_set_ir_library(nano_ir_library_t *lib)
{
    if (!w.built) {
        free(lib);
        return;
    }
    free(s.lib);
    s.lib = lib;
    const char *current = ir_tab_name();
    int tab = 0;
    for (int i = 0; lib && i < lib->count[NANO_IR_FACTORY]; i++) {
        if (strcmp(nano_ir_library_name(lib, NANO_IR_FACTORY, i), current) == 0) tab = 1;
    }
    if (!w.naming) select_tab(tab);
}

void nano_ui_set_ir_folders(const nano_ir_folders_t *folders)
{
    if (!w.built) return;
    if (!s.folders) s.folders = malloc(sizeof(*s.folders));
    if (!s.folders) return;
    *s.folders = *folders;
    if (s.folder != NANO_IR_NO_FOLDER && !open_folder()) s.folder = NANO_IR_NO_FOLDER; /* deleted */
    if (s.mode == MODE_BROWSE || s.mode == MODE_MOVE) reload(w.pager.current);
}
