#include "ui_text_edit.h"

#include <stdio.h>
#include <string.h>

#include "ui_common.h"

#define EDGE 8
#define FIELD_H 34
#define STATUS_DY (FIELD_H + 3)   /* status line, from the field's top */
#define KB_DY (FIELD_H + 20)      /* keyboard top, from the field's top */

struct ui_text_edit {
    ui_text_edit_cfg_t cfg;
    lv_obj_t *field, *status, *kb;
    bool busy;                    /* submitted, waiting for the page */
};

static void show_count(ui_text_edit_t *t)
{
    char s[24];
    snprintf(s, sizeof(s), "%u / %d", (unsigned)strlen(lv_textarea_get_text(t->field)), t->cfg.max_len);
    lv_label_set_text(t->status, s);
    lv_obj_set_style_text_color(t->status, lv_color_hex(C_MUTED), 0);
}

static void show_message(ui_text_edit_t *t, const char *msg, uint32_t color)
{
    lv_label_set_text(t->status, msg);
    lv_obj_set_style_text_color(t->status, lv_color_hex(color), 0);
}

static void set_busy(ui_text_edit_t *t, bool busy)
{
    t->busy = busy;
    if (busy) lv_obj_add_state(t->kb, LV_STATE_DISABLED);
    else lv_obj_remove_state(t->kb, LV_STATE_DISABLED);
    lv_obj_set_style_opa(t->kb, busy ? LV_OPA_40 : LV_OPA_COVER, 0);
}

static void on_changed(lv_event_t *e)
{
    ui_text_edit_t *t = lv_event_get_user_data(e);
    if (!t->busy) show_count(t);
}

static void on_ready(lv_event_t *e)
{
    ui_text_edit_t *t = lv_event_get_user_data(e);
    if (t->busy) return;
    const char *text = lv_textarea_get_text(t->field);
    int n = (int)strlen(text);
    char why[48] = "";
    if (n < t->cfg.min_len) snprintf(why, sizeof(why), "At least %d characters", t->cfg.min_len);
    else if (t->cfg.validate && !t->cfg.validate(text, why, sizeof(why), t->cfg.user) && !why[0]) snprintf(why, sizeof(why), "Not allowed");
    if (why[0]) {
        show_message(t, why, C_ERROR);
        return;
    }
    set_busy(t, true);
    show_message(t, "Saving...", C_MUTED);
    if (t->cfg.on_submit) t->cfg.on_submit(text, t->cfg.user);
}

static void on_delete(lv_event_t *e)
{
    lv_free(lv_event_get_user_data(e));
}

ui_text_edit_t *ui_text_edit_create(lv_obj_t *parent, int32_t y, const ui_text_edit_cfg_t *cfg)
{
    ui_text_edit_t *t = lv_malloc(sizeof(*t));
    if (!t) return NULL;
    memset(t, 0, sizeof(*t));
    t->cfg = *cfg;
    t->cfg.text = NULL; /* copied into the field below */

    lv_obj_t *box = ui_box(parent, 0, y, SCREEN_W, SCREEN_H - y, C_BG);
    lv_obj_set_style_bg_opa(box, LV_OPA_TRANSP, 0);
    lv_obj_add_event_cb(box, on_delete, LV_EVENT_DELETE, t);

    t->field = ui_text_field(box, EDGE, 0, SCREEN_W - 2 * EDGE, FIELD_H, &lv_font_montserrat_20);
    lv_textarea_set_max_length(t->field, (uint32_t)cfg->max_len);
    if (cfg->placeholder) lv_textarea_set_placeholder_text(t->field, cfg->placeholder);
    lv_textarea_set_text(t->field, cfg->text ? cfg->text : "");
    lv_obj_set_style_pad_ver(t->field, 5, 0);
    lv_obj_add_event_cb(t->field, on_changed, LV_EVENT_VALUE_CHANGED, t);
    lv_obj_add_event_cb(t->field, on_ready, LV_EVENT_READY, t); /* OK and Enter: the keyboard forwards them */

    t->status = ui_label(box, &lv_font_montserrat_12, C_MUTED);
    lv_obj_set_pos(t->status, EDGE + 2, STATUS_DY);
    lv_obj_set_width(t->status, SCREEN_W - 2 * EDGE);

    t->kb = ui_keyboard(box, SCREEN_H - y - KB_DY, t->field);

    show_count(t);
    return t;
}

void ui_text_edit_set_error(ui_text_edit_t *t, const char *msg)
{
    if (!t) return;
    set_busy(t, false);
    show_message(t, msg ? msg : "Not saved", C_ERROR);
}

const char *ui_text_edit_text(const ui_text_edit_t *t)
{
    return t ? lv_textarea_get_text(t->field) : "";
}
