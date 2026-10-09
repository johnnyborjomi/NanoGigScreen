#include "ui_value_ctrl.h"

#include <stdlib.h>
#include <string.h>

#include "ui_common.h"

#define EDGE 16
#define STEP_W 62
#define STEP_H 40
#define STEP_GAP 6
#define SLIDER_DY 48     /* slider top, from the value's top */
#define STEPS_DY 90      /* steps top: ~20 px below the slider's touch area */
#define MOVE 1           /* slider units to carry on in the same direction */
#define DOUBLE_MS 500    /* two taps on the value within this = reset */

struct ui_value_ctrl {
    ui_value_ctrl_cfg_t cfg;
    lv_obj_t *box, *value, *slider, *steps[4];
    int raw;                 /* shown value, -1 = unknown */
    int owner;               /* what `raw` belongs to (the preset) */
    bool local;              /* changed here at local_ms */
    uint32_t local_ms;
    int sent;                /* last value handed to on_change (drag dedupe) */
    uint32_t sent_ms;
    int target, target_raw;  /* coarse run: the readout aimed at and the raw value it landed on */
    int drag_pos, drag_dir;  /* slider: accepted position this press, direction of the last move */
    uint32_t click_ms;       /* first tap of a possible double tap */
};

static bool writable(const ui_value_ctrl_t *c)
{
    return c->cfg.on_change != NULL && c->raw >= 0;
}

static void show(ui_value_ctrl_t *c)
{
    char t[24] = "-";
    if (c->raw >= 0) c->cfg.format(c->raw, t, sizeof(t));
    lv_label_set_text(c->value, t);
    /* Leave the slider alone while a finger is on it. */
    if (!lv_obj_has_state(c->slider, LV_STATE_PRESSED)) {
        int raw = c->raw >= 0 ? c->raw : (c->cfg.reset_raw >= 0 ? c->cfg.reset_raw : c->cfg.raw_min);
        lv_slider_set_value(c->slider, c->cfg.raw_to_pos(raw), LV_ANIM_OFF);
    }
    bool w = writable(c);
    lv_obj_t *ctl[5] = { c->slider, c->steps[0], c->steps[1], c->steps[2], c->steps[3] };
    for (int i = 0; i < 5; i++) {
        if (w) lv_obj_remove_state(ctl[i], LV_STATE_DISABLED);
        else lv_obj_add_state(ctl[i], LV_STATE_DISABLED);
        lv_obj_set_style_opa(ctl[i], w ? LV_OPA_COVER : LV_OPA_40, 0);
    }
}

static void send(ui_value_ctrl_t *c, int raw)
{
    if (raw == c->sent) return;
    c->sent = raw;
    c->sent_ms = lv_tick_get();
    c->cfg.on_change(raw, c->cfg.user);
}

/* A change made here: show it now, write it, and hold it against older reports. */
static void set_local(ui_value_ctrl_t *c, int raw)
{
    if (!writable(c)) return;
    raw = raw < c->cfg.raw_min ? c->cfg.raw_min : raw > c->cfg.raw_max ? c->cfg.raw_max : raw;
    c->raw = raw;
    c->local = true;
    c->local_ms = lv_tick_get();
    show(c);
    send(c, raw);
}

/* Fine: the next raw value whose readout differs. Coarse: from the value aimed at (the readout, unless
 * this continues a run of taps) to the raw value with the closest readout to the target. */
static void on_step(lv_event_t *e)
{
    ui_value_ctrl_t *c = lv_event_get_user_data(e);
    if (!writable(c)) return;
    int idx = 0;
    while (idx < 4 && c->steps[idx] != lv_event_get_target_obj(e)) idx++;
    int dir = idx >= 2 ? 1 : -1;
    bool fine = idx == 1 || idx == 2;
    int shown = c->cfg.readout(c->raw);
    if (fine) {
        int raw = c->raw;
        while (raw + dir >= c->cfg.raw_min && raw + dir <= c->cfg.raw_max) {
            raw += dir;
            if (c->cfg.readout(raw) != shown) break;
        }
        if (raw == c->raw) return; /* end of the range */
        c->target_raw = -1;        /* a following coarse step starts from what is shown */
        set_local(c, raw);
        return;
    }
    int lo = c->cfg.readout(c->cfg.raw_min), hi = c->cfg.readout(c->cfg.raw_max);
    int target = (c->raw == c->target_raw ? c->target : shown) + dir * c->cfg.coarse;
    target = target < lo ? lo : target > hi ? hi : target;
    int best = -1, best_err = 0;
    for (int raw = c->raw + dir; raw >= c->cfg.raw_min && raw <= c->cfg.raw_max; raw += dir) {
        int t = c->cfg.readout(raw);
        int err = abs(t - target);
        if (best < 0 || err < best_err) { best = raw; best_err = err; }
        if (dir > 0 ? t >= target : t <= target) break; /* past the target: nothing closer beyond */
    }
    if (best < 0) return;
    c->target = target;
    c->target_raw = best;
    set_local(c, best);
}

/* Follows the finger in moves of MOVE slider units, turns back only after cfg.turn (a resting finger
 * rocks); release keeps the last accepted position, not the lift-off sample. */
static void on_slider(lv_event_t *e)
{
    ui_value_ctrl_t *c = lv_event_get_user_data(e);
    if (!writable(c)) return;
    lv_event_code_t code = lv_event_get_code(e);
    int pos = lv_slider_get_value(c->slider);
    if (code == LV_EVENT_PRESSED) {
        c->drag_pos = c->cfg.raw_to_pos(c->raw);
        c->drag_dir = 0;
    } else if (code == LV_EVENT_VALUE_CHANGED) {
        int d = pos - c->drag_pos, dir = d > 0 ? 1 : -1;
        int need = (c->drag_dir == 0 || dir == c->drag_dir) ? MOVE : c->cfg.turn;
        if (abs(d) < need) {
            lv_slider_set_value(c->slider, c->drag_pos, LV_ANIM_OFF);
            return;
        }
        c->drag_pos = pos;
        c->drag_dir = dir;
        c->raw = c->cfg.pos_to_raw(pos);
        c->target_raw = -1;
        c->local = true;
        c->local_ms = lv_tick_get();
        show(c);
        if (lv_tick_elaps(c->sent_ms) >= (uint32_t)c->cfg.drag_ms) send(c, c->raw);
    } else if (code == LV_EVENT_RELEASED) {
        lv_slider_set_value(c->slider, c->drag_pos, LV_ANIM_OFF);
        set_local(c, c->cfg.pos_to_raw(c->drag_pos));
    }
}

/* Two taps within DOUBLE_MS anywhere on the value (LVGL's own double click also wants both taps
 * within 10 px, which the resistive glass did not manage). */
static void on_value_click(lv_event_t *e)
{
    ui_value_ctrl_t *c = lv_event_get_user_data(e);
    bool second = c->click_ms && lv_tick_elaps(c->click_ms) <= DOUBLE_MS;
    c->click_ms = second ? 0 : lv_tick_get();
    if (!second || c->cfg.reset_raw < 0) return;
    c->target_raw = -1;
    set_local(c, c->cfg.reset_raw);
}

static void on_delete(lv_event_t *e)
{
    lv_free(lv_event_get_user_data(e));
}

ui_value_ctrl_t *ui_value_ctrl_create(lv_obj_t *parent, int32_t y, const ui_value_ctrl_cfg_t *cfg)
{
    ui_value_ctrl_t *c = lv_malloc(sizeof(*c));
    if (!c) return NULL;
    memset(c, 0, sizeof(*c));
    c->cfg = *cfg;
    if (!c->cfg.turn) c->cfg.turn = 4;
    if (!c->cfg.drag_ms) c->cfg.drag_ms = 100;
    if (!c->cfg.hold_ms) c->cfg.hold_ms = 1500;
    c->raw = -1;
    c->owner = -1;
    c->sent = -1;
    c->target_raw = -1;

    /* Across the parent: the screen, or a page right of a pager column (then flush left, a small right margin). */
    lv_obj_update_layout(parent);
    const int32_t w = lv_obj_get_width(parent);
    const int32_t left = w < SCREEN_W ? 0 : EDGE, right = w < SCREEN_W ? w - 8 : w - EDGE;
    int32_t step_w = (right - left - 2 * STEP_GAP - 16) / 4;
    if (step_w > STEP_W) step_w = STEP_W;
    c->box = ui_box(parent, 0, y, w, UI_VALUE_CTRL_HEIGHT, C_BG);
    lv_obj_set_style_bg_opa(c->box, LV_OPA_TRANSP, 0);
    lv_obj_add_event_cb(c->box, on_delete, LV_EVENT_DELETE, c);

    c->value = ui_label(c->box, &lv_font_montserrat_28, C_TEXT);
    lv_obj_set_width(c->value, w);
    lv_obj_set_style_text_align(c->value, LV_TEXT_ALIGN_CENTER, 0);
    if (c->cfg.reset_raw >= 0) {
        lv_obj_set_clickable(c->value, true);
        lv_obj_set_ext_click_area(c->value, 10);
        lv_obj_add_event_cb(c->value, on_value_click, LV_EVENT_CLICKED, c);
    }

    c->slider = lv_slider_create(c->box);
    lv_slider_set_range(c->slider, c->cfg.pos_min, c->cfg.pos_max);
    lv_obj_set_size(c->slider, right - left - 8, 10);
    lv_obj_set_pos(c->slider, left + 4, SLIDER_DY);
    lv_obj_set_ext_click_area(c->slider, 10); /* stops ~20 px above the steps */
    lv_obj_set_style_bg_color(c->slider, lv_color_hex(C_PANEL_2), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(c->slider, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(c->slider, lv_color_hex(C_ON), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(c->slider, lv_color_hex(C_TEXT), LV_PART_KNOB);
    lv_obj_set_style_pad_all(c->slider, 6, LV_PART_KNOB);
    lv_obj_add_event_cb(c->slider, on_slider, LV_EVENT_PRESSED, c);
    lv_obj_add_event_cb(c->slider, on_slider, LV_EVENT_VALUE_CHANGED, c);
    lv_obj_add_event_cb(c->slider, on_slider, LV_EVENT_RELEASED, c);

    const int32_t xs[4] = { left, left + step_w + STEP_GAP, right - 2 * step_w - STEP_GAP, right - step_w };
    for (int i = 0; i < 4; i++) {
        c->steps[i] = ui_button(c->box, xs[i], STEPS_DY, step_w, STEP_H, c->cfg.step_labels[i], &lv_font_montserrat_14, C_PANEL, C_TEXT, on_step, c);
    }
    show(c);
    return c;
}

void ui_value_ctrl_report(ui_value_ctrl_t *c, int raw, int owner)
{
    if (!c) return;
    bool holding = c->local && owner == c->owner && lv_tick_elaps(c->local_ms) < (uint32_t)c->cfg.hold_ms;
    if (owner != c->owner) c->target_raw = -1;
    c->owner = owner;
    if (holding) return;
    c->raw = raw < 0 ? -1 : raw;
    show(c);
}

void ui_value_ctrl_set(ui_value_ctrl_t *c, int raw, int owner)
{
    if (!c) return;
    if (lv_obj_has_state(c->slider, LV_STATE_PRESSED)) return; /* still being moved: its next write wins */
    c->local = false;
    ui_value_ctrl_report(c, raw, owner);
}

int ui_value_ctrl_value(const ui_value_ctrl_t *c)
{
    return c ? c->raw : -1;
}
