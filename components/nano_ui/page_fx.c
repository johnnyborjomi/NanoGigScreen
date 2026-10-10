/*
 * FX editor: one FX block (hold its tile on the gig view), three views on one page:
 *   NANO_VIEW_FX        page 1: the type as a tile in its colour (hold it for the types), the model (hold it for the
 *                       type's models) and On / Off; the next pages hold the parameters, three a page: a slider with
 *                       - / + for a range, buttons for a choice of two or three, < > through a longer one.
 *   NANO_VIEW_FX_TYPE   the types the slot takes, as tiles: tap one for its models.
 *   NANO_VIEW_FX_MODEL  that type's models, six a page: tap one to load it (a live edit), back to the editor.
 * Parameters run on the pedal's own 0..1 and show in the editor's units (nano_fx_params.h). The app reads them while
 * the editor shows and the block is on (a bypassed block is never read: nano_ui_set_fx_params). Only the view showing
 * is built.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "nano_fx_params.h"
#include "nano_models.h"
#include "ui_internal.h"
#include "ui_widgets.h"

#define CONTENT_Y (TOP_H + 4)
#define BODY_Y 2                      /* pager and page top, content coordinates */
#define BODY_H (SCREEN_H - CONTENT_Y - BODY_Y - 4)
#define PAGE_W (SCREEN_W - SETTING_X - 8)
#define ROWS_PER_PAGE 3
#define ROW_H 69
#define BTN_W 44
#define BTN_H 36
#define SLIDER_STEPS 1000
#define DRAG_MS 100                   /* a dragged slider writes at most this often */
#define HOLD_MS 1500                  /* a value changed here ignores older reports this long */
#define TYPES_MAX 8
#define MODEL_ROWS 6
#define MODEL_GAP 4

static const char *const SLOT_NAMES[NANO_FX_SLOT_COUNT] = { "Pre 1", "Pre 2", "Post 1", "Post 2", "Post 3" };

/* The blocks as the last state showed them, the edited block's parameters as the pedal last reported them. */
static struct {
    nano_fx_slot_t fx[NANO_FX_SLOT_COUNT];
    bool on[NANO_FX_SLOT_COUNT];
    int preset;
    int slot;                          /* the edited block */
    float values[NANO_FX_PARAMS_MAX];
    bool known;                        /* `values` hold the pedal's answer for this preset, slot and model */
    bool unreadable;                   /* it answered without any */
    uint32_t local_ms[NANO_FX_PARAMS_MAX]; /* changed here at (0 = not lately) */
    nano_category_t pick;              /* the model list's type */
    nano_view_t model_back;            /* where "<" goes from the model list */
} s = { .preset = -1 };

typedef struct {
    int param;                         /* -1 = an unused row */
    lv_obj_t *value, *slider, *minus, *plus, *seg[3];
    int drag_idx;                      /* slider: the step shown while dragged */
    uint32_t sent_ms;
} param_row_t;

static struct {
    bool built;
    lv_obj_t *root, *title, *content, *page;
    nano_view_t shown;                 /* the view `content` holds (NANO_VIEW_MAIN = none) */
    ui_pager_t pager;
    lv_obj_t *type_tile, *type_short, *type_name, *model_name, *power, *note;
    param_row_t rows[ROWS_PER_PAGE];
    lv_obj_t *mrows[MODEL_ROWS], *mnames[MODEL_ROWS], *mbars[MODEL_ROWS];
} w = { .shown = NANO_VIEW_MAIN };

static const nano_fx_slot_t *cur(void) { return &s.fx[s.slot]; }
static uint32_t cur_type(void) { return nano_fx_model_type(cur()->model); }
static const nano_fx_def_t *cur_def(void) { return nano_fx_def(cur_type()); }
static bool cur_present(void) { return cur()->id[0] != 0; }
static nano_category_t cur_cat(void) { return cur()->model ? cur()->model->category : NANO_CAT_UNKNOWN; }
static bool editable(void) { return s.on[s.slot] && s.known && g_ui.cb.on_fx_param; }

/* The parameter at display position `n` (the editor's order). */
static int param_at(const nano_fx_def_t *d, int n) { return d->order ? d->order[n] : n; }
static int param_pages(void)
{
    const nano_fx_def_t *d = cur_def();
    return 1 + (d ? (d->param_count + ROWS_PER_PAGE - 1) / ROWS_PER_PAGE : 0);
}

/* ---- values: the pedal's 0..1 against the editor's steps and options ------------------------------ */

static int range_steps(const nano_fx_param_t *p) { return (int)lroundf((p->max - p->min) / p->step); }
static int choices(const nano_fx_param_t *p) { return p->kind == NANO_FX_PARAM_ENUM ? p->option_count : range_steps(p) + 1; }

/* The step (range) or option (enum) a 0..1 value stands for. */
static int index_of(const nano_fx_param_t *p, float n)
{
    int last = choices(p) - 1;
    int i = (int)lroundf((n < 0 ? 0 : n > 1 ? 1 : n) * last);
    return i < 0 ? 0 : i > last ? last : i;
}

static float normalized_of(const nano_fx_param_t *p, int idx)
{
    int last = choices(p) - 1;
    return last > 0 ? (float)idx / last : 0;
}

/* - / + on a range: about 150 presses end to end, in steps of 1, 2 or 5 times a power of ten. */
static int button_step(const nano_fx_param_t *p)
{
    static const int NICE[] = { 1, 2, 5, 10, 20, 50, 100, 200, 500, 1000 };
    if (p->kind == NANO_FX_PARAM_ENUM) return 1;
    int target = range_steps(p) / 150;
    for (size_t i = 0; i < sizeof(NICE) / sizeof(NICE[0]); i++) {
        if (NICE[i] >= target) return NICE[i];
    }
    return NICE[sizeof(NICE) / sizeof(NICE[0]) - 1];
}

static void option_text(const nano_fx_param_t *p, int idx, char *out, size_t cap)
{
    const char *o = p->options;
    for (int i = 0; i < idx && o; i++) {
        o = strchr(o, '\n');
        if (o) o++;
    }
    size_t n = 0;
    while (o && o[n] && o[n] != '\n') n++;
    snprintf(out, cap, "%.*s", (int)n, o ? o : "");
}

static void value_text(const nano_fx_param_t *p, int idx, char *out, size_t cap)
{
    if (p->kind == NANO_FX_PARAM_ENUM) {
        option_text(p, idx, out, cap);
        return;
    }
    float v = p->min + idx * p->step;
    if (fabsf(v) < p->step / 2) v = 0; /* no "-0.0" */
    const char *gap = p->unit[0] && strcmp(p->unit, "%") != 0 ? " " : "";
    snprintf(out, cap, "%.*f%s%s", p->decimals, (double)v, gap, p->unit);
}

/* ---- the editor's parameter rows ------------------------------------------------------------------ */

static void enable(lv_obj_t *o, bool on)
{
    if (!o) return;
    if (on) lv_obj_remove_state(o, LV_STATE_DISABLED);
    else lv_obj_add_state(o, LV_STATE_DISABLED);
    lv_obj_set_style_opa(o, on ? LV_OPA_COVER : LV_OPA_40, 0);
}

static void show_row(param_row_t *r)
{
    const nano_fx_def_t *d = cur_def();
    if (r->param < 0 || !d) return;
    const nano_fx_param_t *p = &d->params[r->param];
    bool ok = editable();
    int idx = index_of(p, s.values[r->param]);
    char t[24] = "-";
    if (s.known) value_text(p, idx, t, sizeof(t));
    if (r->value) lv_label_set_text(r->value, t);
    if (r->slider && !lv_obj_has_state(r->slider, LV_STATE_PRESSED)) {
        lv_slider_set_value(r->slider, s.known ? (int32_t)lroundf(s.values[r->param] * SLIDER_STEPS) : 0, LV_ANIM_OFF);
    }
    enable(r->slider, ok);
    enable(r->minus, ok);
    enable(r->plus, ok);
    for (int i = 0; i < 3 && r->seg[i]; i++) {
        bool sel = s.known && i == idx;
        lv_obj_set_style_bg_color(r->seg[i], lv_color_hex(sel ? C_ACCENT : C_PANEL), 0);
        lv_obj_set_style_text_color(lv_obj_get_child(r->seg[i], 0), lv_color_hex(sel ? C_FX_TEXT : C_TEXT), 0);
        enable(r->seg[i], ok);
    }
}

/* A change made here: shown now, written (a dragged slider at most every DRAG_MS), held against older reports. */
static void set_value(param_row_t *r, float n, bool now)
{
    if (!editable()) return;
    s.values[r->param] = n < 0 ? 0 : n > 1 ? 1 : n;
    s.local_ms[r->param] = lv_tick_get() | 1;
    show_row(r);
    if (!now && lv_tick_elaps(r->sent_ms) < DRAG_MS) return;
    r->sent_ms = lv_tick_get();
    g_ui.cb.on_fx_param((uint8_t)s.slot, (uint8_t)r->param, s.values[r->param]);
}

static param_row_t *row_of(lv_event_t *e) { return lv_event_get_user_data(e); }

static void on_step(lv_event_t *e)
{
    param_row_t *r = row_of(e);
    const nano_fx_def_t *d = cur_def();
    if (!d || r->param < 0 || !editable()) return;
    const nano_fx_param_t *p = &d->params[r->param];
    int dir = lv_event_get_target_obj(e) == r->plus ? 1 : -1, k = button_step(p);
    int idx = index_of(p, s.values[r->param]), last = choices(p) - 1;
    /* Land on multiples of the button's step, so a run of taps reads 10, 11, 12 rather than 10.3, 11.3. */
    int next = dir > 0 ? (idx / k + 1) * k : ((idx + k - 1) / k - 1) * k;
    next = next < 0 ? 0 : next > last ? last : next;
    if (next != idx) set_value(r, normalized_of(p, next), true);
}

static void on_slider(lv_event_t *e)
{
    param_row_t *r = row_of(e);
    const nano_fx_def_t *d = cur_def();
    if (!d || r->param < 0 || !editable()) return;
    const nano_fx_param_t *p = &d->params[r->param];
    int idx = index_of(p, lv_slider_get_value(r->slider) / (float)SLIDER_STEPS);
    bool released = lv_event_get_code(e) == LV_EVENT_RELEASED;
    if (idx == r->drag_idx && !released) return;
    r->drag_idx = idx;
    set_value(r, normalized_of(p, idx), released);
    if (released) lv_slider_set_value(r->slider, (int32_t)lroundf(s.values[r->param] * SLIDER_STEPS), LV_ANIM_OFF);
}

static void on_slider_press(lv_event_t *e) { row_of(e)->drag_idx = -1; }

static void on_segment(lv_event_t *e)
{
    param_row_t *r = row_of(e);
    const nano_fx_def_t *d = cur_def();
    if (!d || r->param < 0 || !editable()) return;
    for (int i = 0; i < 3; i++) {
        if (r->seg[i] == lv_event_get_target_obj(e)) set_value(r, normalized_of(&d->params[r->param], i), true);
    }
}

static lv_obj_t *small_button(lv_obj_t *p, int32_t x, int32_t y, int32_t width, const char *text, lv_event_cb_t cb, void *user)
{
    lv_obj_t *b = ui_button(p, x, y, width, BTN_H, text, &lv_font_montserrat_14, C_PANEL, C_TEXT, cb, user);
    lv_obj_set_style_radius(b, 8, 0);
    return b;
}

/* name ......... value
 *  [-]  ===O====  [+]      a range
 *  [ HB ] [ Single ]      two or three options
 *  [<]   1/8 D    [>]      more */
static void build_row(lv_obj_t *page, int32_t y, param_row_t *r, const nano_fx_param_t *p)
{
    lv_obj_t *name = ui_line_label(page, 0, y + 4, PAGE_W / 2, &lv_font_montserrat_14, C_MUTED);
    lv_label_set_text(name, p->name);
    const int32_t cy = y + 28;
    bool segments = p->kind == NANO_FX_PARAM_ENUM && p->option_count <= 3;
    if (segments) {
        const int32_t gap = 6, sw = (PAGE_W - (p->option_count - 1) * gap) / p->option_count;
        for (int i = 0; i < p->option_count; i++) {
            char t[24];
            option_text(p, i, t, sizeof(t));
            r->seg[i] = small_button(page, i * (sw + gap), cy, sw, t, on_segment, r);
        }
        return;
    }
    r->minus = small_button(page, 0, cy, BTN_W, p->kind == NANO_FX_PARAM_ENUM ? LV_SYMBOL_LEFT : LV_SYMBOL_MINUS, on_step, r);
    r->plus = small_button(page, PAGE_W - BTN_W, cy, BTN_W, p->kind == NANO_FX_PARAM_ENUM ? LV_SYMBOL_RIGHT : LV_SYMBOL_PLUS, on_step, r);
    lv_obj_add_event_cb(r->minus, on_step, LV_EVENT_LONG_PRESSED_REPEAT, r);
    lv_obj_add_event_cb(r->plus, on_step, LV_EVENT_LONG_PRESSED_REPEAT, r);
    r->value = ui_label(page, &lv_font_montserrat_20, C_TEXT);
    lv_obj_set_style_text_align(r->value, LV_TEXT_ALIGN_RIGHT, 0);
    if (p->kind == NANO_FX_PARAM_ENUM) {
        /* The option between the arrows. */
        lv_obj_set_width(r->value, PAGE_W - 2 * BTN_W - 12);
        lv_obj_set_style_text_align(r->value, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(r->value, BTN_W + 6, cy + (BTN_H - lv_font_get_line_height(&lv_font_montserrat_20)) / 2);
        return;
    }
    lv_obj_set_width(r->value, PAGE_W / 2);
    lv_obj_set_pos(r->value, PAGE_W / 2, y);
    r->slider = lv_slider_create(page);
    lv_slider_set_range(r->slider, 0, SLIDER_STEPS);
    lv_obj_set_size(r->slider, PAGE_W - 2 * BTN_W - 28, 10);
    lv_obj_set_pos(r->slider, BTN_W + 14, cy + (BTN_H - 10) / 2);
    lv_obj_set_ext_click_area(r->slider, 12);
    lv_obj_set_style_bg_color(r->slider, lv_color_hex(C_PANEL_2), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(r->slider, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(r->slider, lv_color_hex(nano_category_color(cur_cat())), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(r->slider, lv_color_hex(C_TEXT), LV_PART_KNOB);
    lv_obj_set_style_pad_all(r->slider, 6, LV_PART_KNOB);
    lv_obj_add_event_cb(r->slider, on_slider_press, LV_EVENT_PRESSED, r);
    lv_obj_add_event_cb(r->slider, on_slider, LV_EVENT_VALUE_CHANGED, r);
    lv_obj_add_event_cb(r->slider, on_slider, LV_EVENT_RELEASED, r);
}

/* ---- the editor's first page ---------------------------------------------------------------------- */

static void go_types(lv_event_t *e) { (void)e; ui_go(NANO_VIEW_FX_TYPE); }

static void go_models(lv_event_t *e)
{
    (void)e;
    if (cur_cat() == NANO_CAT_UNKNOWN) {
        ui_go(NANO_VIEW_FX_TYPE); /* an empty slot or an unknown model: pick the type first */
        return;
    }
    s.pick = cur_cat();
    s.model_back = NANO_VIEW_FX;
    ui_go(NANO_VIEW_FX_MODEL);
}

static void on_power(lv_event_t *e)
{
    (void)e;
    if (cur_present() && g_ui.cb.on_toggle_fx) g_ui.cb.on_toggle_fx((uint8_t)s.slot, s.on[s.slot]);
}

static void refresh_first(void)
{
    if (!w.type_tile) return;
    nano_category_t cat = cur_cat();
    bool present = cur_present();
    uint32_t color = present ? nano_category_color(cat) : C_PANEL;
    uint32_t text = !present ? C_MUTED : nano_category_light_text(cat) ? C_TEXT : C_FX_TEXT;
    lv_obj_set_style_bg_color(w.type_tile, lv_color_hex(color), 0);
    lv_label_set_text(w.type_short, present && cat != NANO_CAT_UNKNOWN ? nano_category_short(cat) : "-");
    lv_label_set_text(w.type_name, !present ? "Empty" : nano_category_name(cat));
    lv_obj_set_style_text_color(w.type_short, lv_color_hex(text), 0);
    lv_obj_set_style_text_color(w.type_name, lv_color_hex(text), 0);
    lv_label_set_text(w.model_name, cur()->model ? cur()->model->name : present ? cur()->id : "No effect");
    bool on = s.on[s.slot];
    lv_obj_set_style_bg_color(w.power, lv_color_hex(on ? C_ON : C_PANEL), 0);
    lv_obj_t *l = lv_obj_get_child(w.power, 0);
    lv_label_set_text(l, on ? LV_SYMBOL_POWER " On" : LV_SYMBOL_POWER " Off");
    lv_obj_set_style_text_color(l, lv_color_hex(on ? C_FX_TEXT : C_MUTED), 0);
    enable(w.power, present);
    lv_label_set_text(w.note, !present ? "" : !cur_def() ? "No settings known for this model"
                              : !on ? "Turn it on to edit its settings" : !s.known ? (s.unreadable ? "No settings" : "Reading...") : "");
}

static lv_obj_t *hold_box(lv_obj_t *p, int32_t x, int32_t y, int32_t width, int32_t h, uint32_t bg, lv_event_cb_t cb)
{
    lv_obj_t *b = ui_box(p, x, y, width, h, bg);
    lv_obj_set_style_radius(b, 10, 0);
    lv_obj_set_clickable(b, true);
    lv_obj_set_style_border_color(b, lv_color_hex(C_TEXT), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(b, 2, LV_STATE_PRESSED);
    if (g_ui.cb.on_fx_model) lv_obj_add_event_cb(b, cb, LV_EVENT_LONG_PRESSED, NULL);
    return b;
}

/*  [ DRV        ]  [ ⏻ On ]
 *  [ Overdrive  ]
 *  [ Model: Green 808     ]
 *  hold to change / note */
static void build_first(lv_obj_t *page)
{
    const int32_t half = (PAGE_W - 8) / 2;
    w.type_tile = hold_box(page, 0, 0, half, 72, C_PANEL, go_types);
    w.type_short = ui_label(w.type_tile, &lv_font_montserrat_28, C_FX_TEXT);
    lv_obj_align(w.type_short, LV_ALIGN_CENTER, 0, -9);
    w.type_name = ui_label(w.type_tile, &montserrat_medium_12, C_FX_TEXT);
    lv_obj_align(w.type_name, LV_ALIGN_BOTTOM_MID, 0, -5);
    w.power = ui_button(page, half + 8, 0, half, 72, "", &lv_font_montserrat_20, C_PANEL, C_TEXT, on_power, NULL);
    lv_obj_set_style_radius(w.power, 10, 0);
    lv_obj_t *model = hold_box(page, 0, 80, PAGE_W, 58, C_PANEL, go_models);
    lv_obj_t *cap = ui_label(model, &montserrat_medium_12, C_MUTED);
    lv_label_set_text(cap, "Model");
    lv_obj_set_pos(cap, 10, 6);
    w.model_name = ui_line_label(model, 10, 24, PAGE_W - 20, &lv_font_montserrat_20, C_TEXT);
    lv_obj_t *hint = ui_label(page, &montserrat_medium_12, C_DIM);
    lv_label_set_text(hint, g_ui.cb.on_fx_model ? "Hold the type or the model to change it" : "");
    lv_obj_set_pos(hint, 2, 146);
    w.note = ui_label(page, &lv_font_montserrat_14, C_WARN);
    lv_obj_set_pos(w.note, 2, 168);
    lv_obj_set_width(w.note, PAGE_W);
    refresh_first();
}

static void forget_editor_page(void)
{
    w.type_tile = w.type_short = w.type_name = w.model_name = w.power = w.note = NULL;
    memset(w.rows, 0, sizeof(w.rows));
    for (int i = 0; i < ROWS_PER_PAGE; i++) w.rows[i].param = -1;
}

static void show_editor_page(int idx)
{
    lv_obj_clean(w.page);
    forget_editor_page();
    const nano_fx_def_t *d = cur_def();
    if (idx == 0 || !d) {
        build_first(w.page);
        return;
    }
    for (int i = 0; i < ROWS_PER_PAGE; i++) {
        int n = (idx - 1) * ROWS_PER_PAGE + i;
        if (n >= d->param_count) break;
        param_row_t *r = &w.rows[i];
        r->param = param_at(d, n);
        build_row(w.page, i * ROW_H, r, &d->params[r->param]);
        show_row(r);
    }
}

static void refresh_editor(void)
{
    if (w.shown != NANO_VIEW_FX) return;
    refresh_first();
    for (int i = 0; i < ROWS_PER_PAGE; i++) show_row(&w.rows[i]);
}

/* ---- types --------------------------------------------------------------------------------------- */

/* The types slot `slot` takes, in the editor's order of its models. */
static int slot_types(nano_category_t out[TYPES_MAX])
{
    int count, n = 0;
    const uint16_t *types = nano_fx_slot_models(s.slot, &count);
    for (int i = 0; i < count; i++) {
        const nano_fx_model_t *m = nano_fx_model_by_type(types[i]);
        if (!m) continue;
        bool seen = false;
        for (int k = 0; k < n; k++) seen |= out[k] == m->category;
        if (!seen && n < TYPES_MAX) out[n++] = m->category;
    }
    return n;
}

static void on_type(lv_event_t *e)
{
    s.pick = (nano_category_t)(intptr_t)lv_event_get_user_data(e);
    s.model_back = NANO_VIEW_FX_TYPE;
    ui_go(NANO_VIEW_FX_MODEL);
}

static void build_types(lv_obj_t *content)
{
    nano_category_t cats[TYPES_MAX];
    int n = slot_types(cats);
    const int cols = n <= 4 ? 2 : 4, rows = (n + cols - 1) / cols, gap = 6;
    const int32_t x0 = 8, tw = (SCREEN_W - 2 * x0 - (cols - 1) * gap) / cols, th = (BODY_H - (rows - 1) * gap) / (rows ? rows : 1);
    for (int i = 0; i < n; i++) {
        nano_category_t c = cats[i];
        uint32_t text = nano_category_light_text(c) ? C_TEXT : C_FX_TEXT;
        lv_obj_t *t = ui_box(content, x0 + (i % cols) * (tw + gap), BODY_Y + (i / cols) * (th + gap), tw, th, nano_category_color(c));
        lv_obj_set_style_radius(t, 10, 0);
        /* The current type: an outline around the tile (a border would vanish on the white ones). */
        lv_obj_set_style_outline_color(t, lv_color_hex(C_TEXT), 0);
        lv_obj_set_style_outline_pad(t, 2, 0);
        lv_obj_set_style_outline_width(t, c == cur_cat() ? 2 : 0, 0);
        lv_obj_set_style_outline_width(t, 2, LV_STATE_PRESSED);
        lv_obj_set_clickable(t, true);
        lv_obj_add_event_cb(t, on_type, LV_EVENT_CLICKED, (void *)(intptr_t)c);
        lv_obj_t *l = ui_label(t, cols == 2 ? &lv_font_montserrat_32 : &lv_font_montserrat_24, text);
        lv_label_set_text(l, nano_category_short(c));
        lv_obj_align(l, LV_ALIGN_CENTER, 0, -8);
        lv_obj_t *name = ui_label(t, cols == 2 ? &montserrat_medium_12 : &montserrat_medium_10, text);
        lv_label_set_text(name, nano_category_name(c));
        lv_obj_align(name, LV_ALIGN_BOTTOM_MID, 0, -8);
    }
}

/* ---- models ---------------------------------------------------------------------------------------- */

/* Model `n` of the picked type in this slot, 0 past the end. */
static uint32_t model_at(int n)
{
    int count;
    const uint16_t *types = nano_fx_slot_models(s.slot, &count);
    for (int i = 0; i < count; i++) {
        const nano_fx_model_t *m = nano_fx_model_by_type(types[i]);
        if (m && m->category == s.pick && n-- == 0) return types[i];
    }
    return 0;
}

static void show_models(int page)
{
    for (int i = 0; i < MODEL_ROWS; i++) {
        uint32_t type = model_at(page * MODEL_ROWS + i);
        lv_obj_set_hidden(w.mrows[i], !type);
        if (!type) continue;
        lv_label_set_text(w.mnames[i], nano_fx_model_by_type(type)->name);
        lv_obj_set_style_border_width(w.mrows[i], type == cur_type() ? 2 : 0, 0);
    }
}

static void on_model(lv_event_t *e)
{
    uint32_t type = model_at(w.pager.current * MODEL_ROWS + (int)(intptr_t)lv_event_get_user_data(e));
    if (!type) return;
    if (type != cur_type() && g_ui.cb.on_fx_model) g_ui.cb.on_fx_model((uint8_t)s.slot, type);
    ui_go(NANO_VIEW_FX);
}

static void build_models(lv_obj_t *content)
{
    const int32_t row_h = (BODY_H - (MODEL_ROWS - 1) * MODEL_GAP) / MODEL_ROWS;
    ui_pager_create(&w.pager, content, BODY_Y, BODY_H, "Page");
    w.pager.on_show = show_models;
    w.pager.wrap = true;
    for (int i = 0; i < MODEL_ROWS; i++) {
        lv_obj_t *row = ui_box(content, SETTING_X, BODY_Y + i * (row_h + MODEL_GAP), SETTING_RIGHT, row_h, C_PANEL);
        lv_obj_set_style_radius(row, 8, 0);
        lv_obj_set_style_border_color(row, lv_color_hex(C_ON), 0);
        lv_obj_set_clickable(row, true);
        lv_obj_set_style_bg_color(row, lv_color_hex(C_TEXT), LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(row, LV_OPA_30, LV_STATE_PRESSED);
        lv_obj_add_event_cb(row, on_model, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        lv_obj_t *bar = ui_box(row, 6, (row_h - 16) / 2, 5, 16, nano_category_color(s.pick));
        lv_obj_set_style_radius(bar, 2, 0);
        lv_obj_set_clickable(bar, false);
        w.mnames[i] = ui_line_label(row, 18, 0, SETTING_RIGHT - 26, &lv_font_montserrat_14, C_TEXT);
        lv_obj_align(w.mnames[i], LV_ALIGN_LEFT_MID, 18, 0);
        lv_obj_set_hidden(row, true);
        w.mrows[i] = row;
    }
    int n = 0, current = 0;
    for (uint32_t t; (t = model_at(n)) != 0; n++) {
        if (t == cur_type()) current = n / MODEL_ROWS;
    }
    ui_pager_set_count(&w.pager, n ? (n + MODEL_ROWS - 1) / MODEL_ROWS : 1, current);
}

/* ---- page --------------------------------------------------------------------------------------- */

static void forget_content(void)
{
    forget_editor_page();
    memset(w.mrows, 0, sizeof(w.mrows));
    memset(w.mnames, 0, sizeof(w.mnames));
    w.page = NULL;
}

static void set_title(void)
{
    char t[40];
    if (g_ui.view == NANO_VIEW_FX_TYPE) snprintf(t, sizeof(t), "%s: type", SLOT_NAMES[s.slot]);
    else if (g_ui.view == NANO_VIEW_FX_MODEL) snprintf(t, sizeof(t), "%s: %s", SLOT_NAMES[s.slot], nano_category_name(s.pick));
    else snprintf(t, sizeof(t), "%s", SLOT_NAMES[s.slot]);
    lv_label_set_text(w.title, t);
}

/* Rebuild the content for the view showing; `page` = the editor's page to open on. */
static void show_view(nano_view_t view, int page)
{
    /* Async: the tap that changes the view is still being handled by one of the old content's children. */
    if (w.content) {
        lv_obj_set_hidden(w.content, true);
        lv_obj_delete_async(w.content);
    }
    forget_content();
    w.content = ui_box(w.root, 0, CONTENT_Y, SCREEN_W, SCREEN_H - CONTENT_Y, C_BG);
    w.shown = view;
    set_title();
    if (view == NANO_VIEW_FX_TYPE) {
        build_types(w.content);
    } else if (view == NANO_VIEW_FX_MODEL) {
        build_models(w.content);
    } else {
        w.page = ui_box(w.content, SETTING_X, BODY_Y, SCREEN_W - SETTING_X, BODY_H, C_BG);
        ui_pager_create(&w.pager, w.content, BODY_Y, BODY_H, "Page");
        w.pager.on_show = show_editor_page;
        ui_pager_set_count(&w.pager, param_pages(), page);
    }
}

static void on_back(lv_event_t *e)
{
    (void)e;
    if (g_ui.view == NANO_VIEW_FX_MODEL) ui_go(s.model_back);
    else if (g_ui.view == NANO_VIEW_FX_TYPE) ui_go(NANO_VIEW_FX);
    else ui_go_base();
}

static void on_close(lv_event_t *e) { (void)e; ui_go_base(); }

static lv_obj_t *build(lv_obj_t *scr)
{
    ui_overlay_t o = ui_overlay(scr, "", on_back, on_close);
    w.root = o.root;
    w.title = o.title;
    w.content = NULL;
    w.shown = NANO_VIEW_MAIN;
    w.built = true;
    return o.root;
}

static void destroy(void)
{
    w.built = false;
    w.shown = NANO_VIEW_MAIN;
    w.root = w.content = NULL; /* deleted with the root */
    forget_content();
}

static bool is_fx_view(nano_view_t v) { return v == NANO_VIEW_FX || v == NANO_VIEW_FX_TYPE || v == NANO_VIEW_FX_MODEL; }

static void enter(nano_view_t view, nano_view_t from, bool notify)
{
    (void)notify;
    show_view(view, 0);
    if (!is_fx_view(from) && g_ui.cb.on_fx_view) g_ui.cb.on_fx_view((uint8_t)s.slot, true);
}

static void leave(nano_view_t to, bool notify)
{
    (void)to, (void)notify;
    if (g_ui.cb.on_fx_view) g_ui.cb.on_fx_view((uint8_t)s.slot, false);
}

ui_page_t page_fx = { .build = build, .destroy = destroy, .enter = enter, .leave = leave, .needs_link = true };

void nano_ui_open_fx(uint8_t slot)
{
    if (slot >= NANO_FX_SLOT_COUNT) return;
    if (is_fx_view(g_ui.view)) ui_go_base(); /* another slot: close this one first (the app hears both) */
    if (slot != s.slot) {
        s.known = s.unreadable = false;
        memset(s.local_ms, 0, sizeof(s.local_ms));
    }
    s.slot = slot;
    ui_go(NANO_VIEW_FX);
}

void nano_ui_fx_page(int index)
{
    if (w.shown == NANO_VIEW_FX) ui_pager_show(&w.pager, index);
}

/* ---- the app's side ---------------------------------------------------------------------------------- */

void fx_from_state(const nano_state_t *st)
{
    uint32_t type_before = cur_type();
    bool on_before = s.on[s.slot];
    memcpy(s.fx, st->fx, sizeof(s.fx));
    for (int i = 0; i < NANO_FX_SLOT_COUNT; i++) s.on[i] = st->has_bypass && st->fx_on[i] && st->fx[i].id[0];
    bool other = st->active_preset != s.preset || cur_type() != type_before || s.on[s.slot] != on_before;
    s.preset = st->active_preset;
    if (other) {
        /* The app reads the new block's parameters (or none while it is off). */
        s.known = s.unreadable = false;
        memset(s.local_ms, 0, sizeof(s.local_ms));
    }
    if (!w.built) return;
    if (w.shown == NANO_VIEW_FX && cur_type() != type_before) {
        show_view(NANO_VIEW_FX, w.pager.current < param_pages() ? w.pager.current : 0); /* another model: other pages */
    } else if (w.shown == NANO_VIEW_FX_MODEL && cur_type() != type_before) {
        show_models(w.pager.current);
    }
    refresh_editor();
}

void nano_ui_set_fx_params(const float *values, int count, int slot, uint32_t type, int preset, bool fresh)
{
    if (slot != s.slot || preset != s.preset || type != cur_type()) return; /* an answer for what showed before */
    s.unreadable = values == NULL;
    if (values) {
        const nano_fx_def_t *d = cur_def();
        for (int i = 0; d && i < count && i < d->param_count; i++) {
            bool held = s.local_ms[i] && lv_tick_elaps(s.local_ms[i]) < HOLD_MS;
            if (fresh || !held) s.values[i] = values[i];
        }
        /* A slider under a finger keeps its value: its next write wins. */
        s.known = true;
    } else {
        s.known = false;
    }
    refresh_editor();
}
