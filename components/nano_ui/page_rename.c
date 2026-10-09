/*
 * Rename page: a ui_text_edit with the preset's name. The pedal stores a new name at once (no save needed); the
 * page waits for its answer (nano_ui_rename_result) and closes on success. Built on open (the keyboard).
 */
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "ui_internal.h"
#include "ui_text_edit.h"
#include "ui_widgets.h"

static uint8_t s_index;
static nano_view_t s_from = NANO_VIEW_MAIN; /* where "<" goes back to */
static ui_text_edit_t *s_edit;              /* freed with the page */

static void on_back(lv_event_t *e) { (void)e; ui_go(s_from); }
static void on_close(lv_event_t *e) { (void)e; ui_go_base(); }

/* The pedal accepts any name; like DrD85's controller, no two presets share one (ignoring letter case).
 * Spaces at either end would be invisible on the screen. */
static bool validate(const char *text, char *why, size_t cap, void *user)
{
    (void)user;
    if (text[0] == ' ' || text[strlen(text) - 1] == ' ') {
        snprintf(why, cap, "No space at the start or end");
        return false;
    }
    for (int i = 0; g_ui.meta && i < NANO_PRESET_COUNT; i++) {
        if (i != s_index && strcasecmp(g_ui.meta->presets[i].name, text) == 0) {
            snprintf(why, cap, "Preset %d already has this name", i + 1);
            return false;
        }
    }
    return true;
}

static void submit(const char *text, void *user)
{
    (void)user;
    const char *old = g_ui.meta ? g_ui.meta->presets[s_index].name : "";
    if (strcmp(text, old) == 0 || !g_ui.cb.on_rename_preset) {
        ui_go(s_from); /* nothing to write */
        return;
    }
    g_ui.cb.on_rename_preset(s_index, text);
}

static lv_obj_t *build(lv_obj_t *scr)
{
    char label[8], title[32];
    nano_preset_label(s_index, g_ui.per_bank, g_ui.label_style, label, sizeof(label));
    snprintf(title, sizeof(title), "Rename preset %s", label);
    ui_overlay_t o = ui_overlay(scr, title, on_back, on_close);
    const ui_text_edit_cfg_t cfg = {
        .text = g_ui.meta ? g_ui.meta->presets[s_index].name : "",
        .placeholder = "Preset name",
        .min_len = 4, .max_len = NANO_PRESET_NAME_MAX,
        .validate = validate,
        .on_submit = submit,
    };
    s_edit = ui_text_edit_create(o.root, TOP_H + 8, &cfg);
    return o.root;
}

static void destroy(void) { s_edit = NULL; }

static void leave(nano_view_t to, bool notify)
{
    (void)notify;
    if (to != NANO_VIEW_PRESETS) presets_forget_return_bank();
}

ui_page_t page_rename = { .build = build, .destroy = destroy, .leave = leave, .needs_link = true };

void nano_ui_open_rename(uint8_t index)
{
    if (index >= NANO_PRESET_COUNT || !g_ui.cb.on_rename_preset) return;
    s_index = index;
    s_from = g_ui.view == NANO_VIEW_PRESETS ? NANO_VIEW_PRESETS : g_ui.base_view;
    ui_show_view(NANO_VIEW_RENAME, true);
}

void nano_ui_rename_result(uint8_t index, bool ok, const char *msg)
{
    if (g_ui.view != NANO_VIEW_RENAME || index != s_index) return;
    if (ok) ui_go(s_from);
    else ui_text_edit_set_error(s_edit, msg);
}
