/*
 * The gig view: top bar (link status, outputs muted, tempo, menu), preset row (prev, label + footswitch badges +
 * name, next), gate and LIST around the capture / IR lines, five FX tiles in category colours, and the expression
 * pedal (a side bar and a track on each assigned tile). Built once; the other pages draw over it.
 */
#include <stdio.h>
#include <string.h>

#include "nano_models.h"
#include "ui_internal.h"
#include "ui_widgets.h"

#define ROW_Y 24
#define ROW_H 94
#define NAV_W 36          /* prev / next buttons: narrower and shorter than the row, centred on it */
#define NAV_H 66
#define LINES_Y 120       /* capture / IR / gate row, pulled up to give the tiles room for the category tag */
#define LINES_H 40
#define GATE_W 40
#define GATE_H 26
#define TILE_Y 164
#define TILE_W 60
#define TILE_H 74
#define TILE_TAG_Y 1      /* category tag: right under the top border, same spot on and off */
#define TILE_NAME_DY 6    /* name centred in the space below the tag */
#define TILE_EXP_H 6      /* per-tile expression track at the bottom of an assigned FX tile */
#define TILE_EXP_INSET 4
#define TILE_EXP_W (TILE_W - 2 * 2 - 2 * TILE_EXP_INSET)
#define EXP_BAR_Y ROW_Y   /* the side bar: from the preset row to the tiles */
#define EXP_BAR_H (TILE_Y + TILE_H - ROW_Y)
#define BADGE_W 26
#define BADGE_H 14
#define BADGE_GAP 3

/* Footswitch badges (web app: IA / IIA yellow, IB / IIB indigo), shown under the bank label. */
static const char *const FS_NAMES[4] = { "IA", "IB", "IIA", "IIB" };
static const uint32_t FS_BG[4] = { 0xF5C542, 0x6A5CFF, 0xF5C542, 0x6A5CFF };
static const uint32_t FS_FG[4] = { 0x111111, 0xFFFFFF, 0x111111, 0xFFFFFF };
#define C_TILE_EMPTY_BORDER 0x262D37

static struct {
    lv_obj_t *status_dot, *status, *tempo, *mute_badge, *gate, *list_btn;
    lv_obj_t *preset_label, *preset_name, *prev, *next, *fs_badges[4];
    lv_obj_t *capture_dot, *capture, *ir_dot, *ir;
    lv_obj_t *tiles[NANO_FX_SLOT_COUNT], *tile_names[NANO_FX_SLOT_COUNT], *tile_tags[NANO_FX_SLOT_COUNT];
    lv_obj_t *tile_exp[NANO_FX_SLOT_COUNT], *tile_exp_band[NANO_FX_SLOT_COUNT], *tile_exp_fill[NANO_FX_SLOT_COUNT];
    lv_obj_t *exp_bar, *exp_fill;
} w;

static struct {
    bool gate_on;
    bool tile_present[NANO_FX_SLOT_COUNT], tile_on[NANO_FX_SLOT_COUNT];
    uint32_t tile_color[NANO_FX_SLOT_COUNT];
    uint8_t footswitch[4];
    bool footswitch_known;
    int exp_pos;                    /* -1 = unknown (drawn at the heel) */
    bool exp_assign_valid, exp_values_valid;
    nano_exp_assignments_t exp_assign;
    nano_exp_values_t exp_values;
} s = { .exp_pos = -1 };

/* ---- fonts ---------------------------------------------------------------------- */

/* Largest font whose wrapped text fits the box in at most two lines. */
static const lv_font_t *fit_font(const char *text, int32_t max_w, int32_t max_h)
{
    static const lv_font_t *const fonts[] = { &lv_font_montserrat_40, &lv_font_montserrat_32, &lv_font_montserrat_28, &lv_font_montserrat_24, &lv_font_montserrat_20 };
    for (size_t i = 0; i < sizeof(fonts) / sizeof(fonts[0]); i++) {
        lv_point_t size;
        lv_text_get_size(&size, text, fonts[i], 0, 0, max_w, LV_TEXT_FLAG_NONE);
        int32_t line_h = lv_font_get_line_height(fonts[i]);
        if (size.y <= max_h && size.y <= 2 * line_h + 1) return fonts[i];
    }
    return &lv_font_montserrat_20;
}

/* Tile names wrap on spaces; a single word wider than the tile, or a name that needs more than
 * three lines ("Solid State Comp (M)"), drops to the 10 px font. */
static const lv_font_t *tile_font(const char *text, int32_t max_w)
{
    const char *p = text;
    while (*p) {
        const char *end = p;
        while (*end && *end != ' ') end++;
        char word[32];
        size_t n = (size_t)(end - p) < sizeof(word) - 1 ? (size_t)(end - p) : sizeof(word) - 1;
        memcpy(word, p, n);
        word[n] = '\0';
        lv_point_t size;
        lv_text_get_size(&size, word, &montserrat_medium_12, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        if (size.x > max_w) return &montserrat_medium_10;
        p = *end ? end + 1 : end;
    }
    lv_point_t size;
    lv_text_get_size(&size, text, &montserrat_medium_12, 0, 0, max_w, LV_TEXT_FLAG_NONE);
    if (size.y > 3 * lv_font_get_line_height(&montserrat_medium_12)) return &montserrat_medium_10;
    return &montserrat_medium_12;
}

/* ---- events ------------------------------------------------------------------------ */

static void on_prev(lv_event_t *e) { (void)e; if (g_ui.cb.on_prev_preset) g_ui.cb.on_prev_preset(); }
static void on_next(lv_event_t *e) { (void)e; if (g_ui.cb.on_next_preset) g_ui.cb.on_next_preset(); }
static void on_menu(lv_event_t *e) { (void)e; ui_go(NANO_VIEW_MENU); }
static void on_open_presets(lv_event_t *e) { (void)e; ui_go(NANO_VIEW_PRESETS); }
static void on_open_capture(lv_event_t *e) { (void)e; ui_go(NANO_VIEW_CAPTURE); }
static void on_open_ir(lv_event_t *e) { (void)e; ui_go(NANO_VIEW_IR); }
static void on_preset_name_long(lv_event_t *e) { (void)e; nano_ui_open_rename(g_ui.preset); }

static void on_gate_clicked(lv_event_t *e)
{
    (void)e;
    if (g_ui.cb.on_toggle_gate) g_ui.cb.on_toggle_gate(s.gate_on);
}

static void on_tile_clicked(lv_event_t *e)
{
    uint8_t slot = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
    if (slot >= NANO_FX_SLOT_COUNT || !s.tile_present[slot]) return;
    if (g_ui.cb.on_toggle_fx) g_ui.cb.on_toggle_fx(slot, s.tile_on[slot]);
}

/* ---- build ----------------------------------------------------------------------------- */

/* A transparent box over a line: a long press opens `cb`'s page (a tap does nothing). */
static void hold_area(lv_obj_t *parent, int32_t x, int32_t y, int32_t width, lv_event_cb_t cb)
{
    lv_obj_t *hit = ui_box(parent, x, y, width, 20, C_BG);
    lv_obj_set_style_bg_opa(hit, LV_OPA_TRANSP, 0);
    lv_obj_set_clickable(hit, true);
    lv_obj_add_event_cb(hit, cb, LV_EVENT_LONG_PRESSED, NULL);
}

static void build_top_bar(lv_obj_t *root)
{
    /* Status dot + text, outputs muted, tempo, menu button. */
    w.status_dot = ui_dot(root, 8, 8, 8);
    w.status = ui_line_label(root, 22, 5, 112, &lv_font_montserrat_12, C_MUTED);
    /* Outputs 1/2 muted (Cortex Cloud's global switch): a red badge you cannot miss on stage. */
    w.mute_badge = ui_box(root, 138, 3, 50, 16, C_ERROR);
    lv_obj_set_style_radius(w.mute_badge, 4, 0);
    lv_obj_t *mute_l = ui_label(w.mute_badge, &montserrat_medium_10, C_FX_TEXT);
    lv_label_set_text(mute_l, "MUTED");
    lv_obj_center(mute_l);
    lv_obj_set_hidden(w.mute_badge, true);
    w.tempo = ui_label(root, &lv_font_montserrat_12, C_ON);
    lv_obj_set_pos(w.tempo, 190, 5);
    lv_obj_set_width(w.tempo, 80);
    lv_obj_set_style_text_align(w.tempo, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_t *menu = ui_button(root, SCREEN_W - 44, 0, 44, TOP_H + 4, LV_SYMBOL_LIST, &lv_font_montserrat_14, C_BG, C_MUTED, on_menu, NULL);
    lv_obj_set_ext_click_area(menu, 6);
}

static void build_preset_row(lv_obj_t *root)
{
    /* prev | label + name | next. */
    const int32_t nav_y = ROW_Y + (ROW_H - NAV_H) / 2;
    /* A tap steps, a hold opens the presets list (short clicks only: no step when the hold is let go). */
    w.prev = ui_button(root, EDGE_X, nav_y, NAV_W, NAV_H, LV_SYMBOL_LEFT, &lv_font_montserrat_20, C_PANEL, C_MUTED, NULL, NULL);
    w.next = ui_button(root, RIGHT_X - NAV_W, nav_y, NAV_W, NAV_H, LV_SYMBOL_RIGHT, &lv_font_montserrat_20, C_PANEL, C_MUTED, NULL, NULL);
    lv_obj_add_event_cb(w.prev, on_prev, LV_EVENT_SHORT_CLICKED, NULL);
    lv_obj_add_event_cb(w.next, on_next, LV_EVENT_SHORT_CLICKED, NULL);
    lv_obj_add_event_cb(w.prev, on_open_presets, LV_EVENT_LONG_PRESSED, NULL);
    lv_obj_add_event_cb(w.next, on_open_presets, LV_EVENT_LONG_PRESSED, NULL);
    w.preset_label = ui_label(root, &lv_font_montserrat_24, C_TEXT);
    lv_obj_set_pos(w.preset_label, NAV_W + 6, ROW_Y + 4);
    w.preset_name = ui_label(root, &lv_font_montserrat_32, C_TEXT);
    lv_label_set_long_mode(w.preset_name, LV_LABEL_LONG_WRAP);
    lv_label_set_text(w.preset_name, "NanoGig");
    /* Long press on the label or the name: rename (a tap does nothing, so no keyboard by accident). */
    lv_obj_t *name_parts[2] = { w.preset_label, w.preset_name };
    for (int i = 0; i < 2; i++) {
        lv_obj_set_clickable(name_parts[i], true);
        lv_obj_add_event_cb(name_parts[i], on_preset_name_long, LV_EVENT_LONG_PRESSED, NULL);
    }
    for (int i = 0; i < 4; i++) {
        lv_obj_t *b = ui_box(root, 0, 0, BADGE_W, BADGE_H, FS_BG[i]);
        lv_obj_set_style_radius(b, 4, 0);
        lv_obj_t *l = ui_label(b, &montserrat_medium_10, FS_FG[i]);
        lv_label_set_text(l, FS_NAMES[i]);
        lv_obj_center(l);
        lv_obj_set_hidden(b, true);
        w.fs_badges[i] = b;
    }
}

static void build_lines(lv_obj_t *root)
{
    /* Gate button, then the capture / IR lines, then LIST mirroring the gate at the right edge. */
    const int32_t btn_y = LINES_Y + (LINES_H - GATE_H) / 2;
    w.gate = ui_button(root, EDGE_X, btn_y, GATE_W, GATE_H, "GATE", &montserrat_medium_10, C_OFF, C_TEXT, on_gate_clicked, NULL);
    lv_obj_set_style_radius(w.gate, 7, 0);
    lv_obj_set_ext_click_area(w.gate, 8);
    w.list_btn = ui_button(root, RIGHT_X - GATE_W, btn_y, GATE_W, GATE_H, "LIST", &montserrat_medium_10, C_OFF, C_TEXT, on_open_presets, NULL);
    lv_obj_set_style_radius(w.list_btn, 7, 0);
    lv_obj_set_ext_click_area(w.list_btn, 8);
    const int32_t lx = EDGE_X + GATE_W + 10;
    const int32_t lw = RIGHT_X - GATE_W - 8 - (lx + 15);
    w.capture_dot = ui_dot(root, lx, LINES_Y + 6, 9);
    w.capture = ui_line_label(root, lx + 15, LINES_Y + 2, lw, &lv_font_montserrat_12, C_TEXT);
    /* Hold a line (dot or name) for its tab of the Capture / IR page, like the preset name for rename. */
    hold_area(root, lx - 4, LINES_Y, 15 + lw + 8, on_open_capture);
    hold_area(root, lx - 4, LINES_Y + 20, 15 + lw + 8, on_open_ir);
    w.ir_dot = ui_dot(root, lx, LINES_Y + 26, 9);
    w.ir = ui_line_label(root, lx + 15, LINES_Y + 22, lw, &lv_font_montserrat_12, C_MUTED);
}

static void build_tiles(lv_obj_t *root)
{
    /* pre1 pre2 | post1 post2 post3. */
    const int32_t gap = 3, group_gap = 2; /* 5 x 60 + 4 x 3 + 2 = 314: from EDGE_X to RIGHT_X */
    for (int i = 0; i < NANO_FX_SLOT_COUNT; i++) {
        lv_obj_t *t = ui_box(root, EDGE_X + i * (TILE_W + gap) + (i >= 2 ? group_gap : 0), TILE_Y, TILE_W, TILE_H, C_PANEL);
        lv_obj_set_style_radius(t, 10, 0);
        lv_obj_set_style_border_width(t, 3, 0);
        lv_obj_set_style_border_side(t, LV_BORDER_SIDE_TOP, 0);
        lv_obj_set_style_border_color(t, lv_color_hex(C_DIM), 0);
        lv_obj_set_style_pad_all(t, 2, 0);
        lv_obj_set_clickable(t, true);
        lv_obj_set_ext_click_area(t, 6);
        lv_obj_add_event_cb(t, on_tile_clicked, LV_EVENT_CLICKED, (void *)(uintptr_t)i);
        lv_obj_t *name = ui_label(t, &montserrat_medium_12, C_DIM);
        lv_obj_set_width(name, TILE_W - 4);
        lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_long_mode(name, LV_LABEL_LONG_WRAP);
        lv_obj_align(name, LV_ALIGN_CENTER, 0, TILE_NAME_DY);
        lv_label_set_text(name, i < 2 ? "PRE" : "POST");
        /* Category tag ("CMP", "DLY", ...) under the top border; it never moves, only recolours. */
        lv_obj_t *tag = ui_label(t, &montserrat_bold_10, C_DIM);
        lv_obj_set_width(tag, TILE_W - 4);
        lv_obj_set_style_text_align(tag, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(tag, 0, TILE_TAG_Y);
        lv_label_set_text(tag, "");
        w.tiles[i] = t;
        w.tile_names[i] = name;
        w.tile_tags[i] = tag;
        /* Expression track along the bottom edge (inside the 2 px padding): black track, translucent
         * band for the assigned range, solid fill for the value. Hidden until the pedal moves. */
        lv_obj_t *track = ui_box(t, TILE_EXP_INSET, TILE_H - 2 * 2 - TILE_EXP_H - 3, TILE_EXP_W, TILE_EXP_H, 0x000000);
        lv_obj_set_style_bg_opa(track, LV_OPA_70, 0);
        lv_obj_set_style_radius(track, TILE_EXP_H / 2, 0);
        lv_obj_t *band = ui_box(track, 0, 0, TILE_EXP_W, TILE_EXP_H, C_TEXT);
        lv_obj_set_style_bg_opa(band, LV_OPA_40, 0);
        lv_obj_set_style_radius(band, TILE_EXP_H / 2, 0);
        lv_obj_t *fill = ui_box(track, 0, 1, 0, TILE_EXP_H - 2, C_TEXT);
        lv_obj_set_style_radius(fill, (TILE_EXP_H - 2) / 2, 0);
        lv_obj_set_hidden(track, true);
        w.tile_exp[i] = track;
        w.tile_exp_band[i] = band;
        w.tile_exp_fill[i] = fill;
    }

    /* Expression pedal position: a thin orange bar in the right gutter, filling from the heel (bottom). */
    w.exp_bar = ui_box(root, EXP_BAR_X, EXP_BAR_Y, EXP_BAR_W, EXP_BAR_H, C_PANEL_2);
    lv_obj_set_style_radius(w.exp_bar, 1, 0);
    w.exp_fill = ui_box(root, EXP_BAR_X, EXP_BAR_Y + EXP_BAR_H, EXP_BAR_W, 0, C_WARN);
    lv_obj_set_style_radius(w.exp_fill, 1, 0);
    lv_obj_set_hidden(w.exp_bar, true);
    lv_obj_set_hidden(w.exp_fill, true);
}

static lv_obj_t *build(lv_obj_t *scr)
{
    lv_obj_t *root = ui_box(scr, 0, 0, SCREEN_W, SCREEN_H, C_BG);
    build_top_bar(root);
    build_preset_row(root);
    build_lines(root);
    build_tiles(root);
    return root;
}

ui_page_t page_main = { .build = build, .keep = true, .underlay = true, .needs_link = true };

/* ---- expression pedal ------------------------------------------------------------------- */

/* Redraw the expression indicators from the position, the assignments and the last values. */
void main_refresh_expression(void)
{
    bool live = g_ui.exp_show;
    int pos = s.exp_pos < 0 ? 0 : s.exp_pos;
    lv_obj_set_hidden(w.exp_bar, !live);
    lv_obj_set_hidden(w.exp_fill, !live);
    if (live) {
        int32_t h = (EXP_BAR_H * pos + 127) / 254;
        lv_obj_set_size(w.exp_fill, EXP_BAR_W, h);
        lv_obj_set_pos(w.exp_fill, EXP_BAR_X, EXP_BAR_Y + EXP_BAR_H - h);
    }
    const int32_t tw = TILE_EXP_W;
    for (int i = 0; i < NANO_FX_SLOT_COUNT; i++) {
        const nano_exp_range_t *r = s.exp_assign_valid ? &s.exp_assign.fx_range[i] : NULL;
        bool has_range = r && r->assigned;
        bool has_bypass = s.exp_assign_valid && s.exp_assign.fx_bypass_mode[i] != 0;
        bool show = live && s.tile_present[i] && (has_range || has_bypass);
        lv_obj_set_hidden(w.tile_exp[i], !show);
        if (!show) continue;
        lv_obj_set_style_bg_color(w.tile_exp_band[i], lv_color_hex(s.tile_color[i]), 0);
        lv_obj_set_style_bg_color(w.tile_exp_fill[i], lv_color_hex(s.tile_color[i]), 0);
        int32_t fill_w;
        if (has_range) {
            /* Band = the range set in Cortex Cloud; fill = the value, which already lives inside it. Until
             * the first values event of this preset, derive it from the position as the pedal maps it. */
            int32_t bx = tw * r->min / 255, bw = tw * (r->max - r->min) / 255;
            if (bw < 2) bw = 2;
            lv_obj_set_pos(w.tile_exp_band[i], bx, 0);
            lv_obj_set_width(w.tile_exp_band[i], bw);
            int v = s.exp_values_valid && s.exp_values.fx_value[i] >= 0 ? s.exp_values.fx_value[i] : r->min + (r->max - r->min) * pos / 254;
            fill_w = tw * v / 255;
        } else {
            /* Bypass only: the whole track is the band; full when the switch is engaged (heel side of mid-travel). */
            lv_obj_set_pos(w.tile_exp_band[i], 0, 0);
            lv_obj_set_width(w.tile_exp_band[i], tw);
            bool engaged = s.exp_values_valid && s.exp_values.fx_bypass[i] >= 0 ? s.exp_values.fx_bypass[i] != 0 : pos < 127;
            fill_w = engaged ? tw : 0;
        }
        if (fill_w > tw) fill_w = tw;
        lv_obj_set_width(w.tile_exp_fill[i], fill_w);
        lv_obj_set_pos(w.tile_exp_fill[i], 0, 1);
    }
}

void nano_ui_set_expression(int position)
{
    s.exp_pos = position < 0 ? -1 : position > 254 ? 254 : position;
    main_refresh_expression();
}

void nano_ui_set_expression_assignments(const nano_exp_assignments_t *a)
{
    s.exp_assign_valid = a != NULL;
    if (a) s.exp_assign = *a;
    main_refresh_expression();
}

void nano_ui_set_expression_values(const nano_exp_values_t *v)
{
    s.exp_values_valid = v != NULL;
    if (v) s.exp_values = *v;
    main_refresh_expression();
}

/* ---- top bar ---------------------------------------------------------------------------- */

void main_refresh_mute(void)
{
    lv_obj_set_hidden(w.mute_badge, !g_ui.outputs_muted);
}

void main_refresh_tempo(void)
{
    if (g_ui.tempo_bpm <= 0) {
        lv_label_set_text(w.tempo, "");
        return;
    }
    char t[20];
    snprintf(t, sizeof(t), g_ui.tempo_tapping ? "TAP %d" : "%d BPM", (int)(g_ui.tempo_bpm + 0.5f));
    lv_label_set_text(w.tempo, t);
    lv_obj_set_style_text_color(w.tempo, lv_color_hex(g_ui.tempo_tapping ? C_WARN : C_ON), 0);
}

void nano_ui_set_status(const char *text, bool connected)
{
    lv_label_set_text(w.status, text);
    lv_obj_set_style_bg_color(w.status_dot, lv_color_hex(connected ? C_ON : C_WARN), 0);
    connect_set_status(text, connected);
}

/* ---- preset row ------------------------------------------------------------------------- */

static void layout_preset_row(void)
{
    /* Which footswitch badges apply to the shown preset. */
    int shown[4], n = 0;
    if (s.footswitch_known && lv_label_get_text(w.preset_label)[0]) {
        for (int i = 0; i < 4; i++) if (s.footswitch[i] == g_ui.preset) shown[n++] = i;
    }
    for (int i = 0; i < 4; i++) lv_obj_set_hidden(w.fs_badges[i], true);
    int badge_cols = n >= 2 ? 2 : n;
    int32_t badges_w = badge_cols ? badge_cols * BADGE_W + (badge_cols - 1) * BADGE_GAP : 0;

    /* Everything top-aligned with the nav buttons, so nothing jumps between one- and two-line names. */
    const int32_t top = ROW_Y + (ROW_H - NAV_H) / 2;
    const int32_t max_h = ROW_Y + ROW_H - top - 2;
    lv_obj_update_layout(w.preset_label);
    int32_t label_w = lv_obj_get_width(w.preset_label);
    int32_t col_w = label_w > badges_w ? label_w : badges_w;
    int32_t col_x = EDGE_X + NAV_W + 4;
    int32_t x = col_x + col_w + (col_w ? 6 : 0);
    int32_t width = RIGHT_X - NAV_W - 4 - x;
    const lv_font_t *font = fit_font(lv_label_get_text(w.preset_name), width, max_h);
    lv_obj_set_style_text_font(w.preset_name, font, 0);
    lv_obj_set_width(w.preset_name, width);
    lv_obj_set_pos(w.preset_name, x, top);

    /* Label at the top of the column, badges under it (two per row). */
    int32_t label_h = lv_font_get_line_height(&lv_font_montserrat_24);
    lv_obj_set_pos(w.preset_label, col_x, top + 2);
    for (int k = 0; k < n; k++) {
        int32_t bx = col_x + (k % 2) * (BADGE_W + BADGE_GAP);
        int32_t by = top + 2 + label_h + 2 + (k / 2) * (BADGE_H + BADGE_GAP);
        lv_obj_set_pos(w.fs_badges[shown[k]], bx, by);
        lv_obj_set_hidden(w.fs_badges[shown[k]], false);
    }
}

void nano_ui_set_footswitches(const uint8_t fs[4])
{
    memcpy(s.footswitch, fs, 4);
    s.footswitch_known = true;
    layout_preset_row();
}

void nano_ui_set_preset(uint8_t index, const nano_metadata_t *meta)
{
    g_ui.preset = index;
    g_ui.meta = meta;
    presets_refresh();
    char label[8];
    nano_preset_label(index, g_ui.per_bank, g_ui.label_style, label, sizeof(label));
    lv_label_set_text(w.preset_label, label);
    lv_obj_set_style_text_color(w.preset_label, lv_color_hex(UI_SLOT_COLORS[(index % g_ui.per_bank) & 7]), 0);
    char fallback[24];
    lv_label_set_text(w.preset_name, ui_preset_name(index, fallback, sizeof(fallback)));
    layout_preset_row();
}

/* ---- the whole view from a state -------------------------------------------------------- */

static void show_tile(int i, const nano_fx_slot_t *fx, bool on)
{
    bool present = fx->id[0] != 0;
    s.tile_present[i] = present;
    s.tile_on[i] = on;
    nano_category_t cat = fx->model ? fx->model->category : NANO_CAT_UNKNOWN;
    uint32_t color = nano_category_color(cat);
    s.tile_color[i] = color;
    const char *name = fx->model ? fx->model->name : (present ? fx->id : "");
    lv_label_set_text(w.tile_names[i], name);
    lv_obj_set_style_text_font(w.tile_names[i], tile_font(name, TILE_W - 4), 0);
    lv_label_set_text(w.tile_tags[i], present ? nano_category_short(cat) : "");
    if (!present) {
        lv_obj_set_style_bg_color(w.tiles[i], lv_color_hex(C_BG), 0);
        lv_obj_set_style_border_color(w.tiles[i], lv_color_hex(C_TILE_EMPTY_BORDER), 0);
        lv_obj_set_style_text_color(w.tile_names[i], lv_color_hex(C_DIM), 0);
    } else if (on) {
        uint32_t text = nano_category_light_text(cat) ? C_TEXT : C_FX_TEXT;
        lv_obj_set_style_bg_color(w.tiles[i], lv_color_hex(color), 0);
        lv_obj_set_style_border_color(w.tiles[i], lv_color_hex(color), 0);
        lv_obj_set_style_text_color(w.tile_names[i], lv_color_hex(text), 0);
        lv_obj_set_style_text_color(w.tile_tags[i], lv_color_mix(lv_color_hex(text), lv_color_hex(color), 170), 0);
    } else {
        lv_obj_set_style_bg_color(w.tiles[i], lv_color_hex(C_OFF), 0);
        lv_obj_set_style_border_color(w.tiles[i], lv_color_mix(lv_color_hex(color), lv_color_hex(C_OFF), 140), 0);
        lv_obj_set_style_text_color(w.tile_names[i], lv_color_hex(C_TEXT), 0);
        /* Off: the tag carries the category colour, a touch brighter than the border. */
        lv_obj_set_style_text_color(w.tile_tags[i], lv_color_mix(lv_color_hex(color), lv_color_hex(C_OFF), 190), 0);
    }
}

void nano_ui_set_state(const nano_state_t *st, const nano_metadata_t *meta)
{
    memcpy(s.footswitch, st->footswitch, 4);
    s.footswitch_known = true;
    nano_ui_set_preset(st->active_preset, meta);
    if ((!meta || !meta->presets[st->active_preset].name[0]) && st->capture_name[0]) {
        /* No cached name yet: the capture name is the most recognisable thing we have. */
        lv_label_set_text(w.preset_name, st->capture_name);
        layout_preset_row();
    }
    ui_line_set(w.capture_dot, w.capture, st->capture_name, st->capture_on, "No capture");
    ui_line_set(w.ir_dot, w.ir, st->ir_short_name, st->cab_on, "No IR");
    source_from_state(st);
    s.gate_on = st->gate_on;
    lv_obj_set_style_bg_color(w.gate, lv_color_hex(st->gate_on ? nano_category_color(NANO_CAT_UTILITY) : C_OFF), 0);
    lv_obj_set_style_text_color(lv_obj_get_child(w.gate, 0), lv_color_hex(st->gate_on ? C_FX_TEXT : C_TEXT), 0);
    nano_ui_set_tempo(st->tempo_bpm, false);
    for (int i = 0; i < NANO_FX_SLOT_COUNT; i++) show_tile(i, &st->fx[i], st->fx[i].id[0] && st->has_bypass && st->fx_on[i]);
    main_refresh_expression();
}

void nano_ui_set_stale(bool stale)
{
    if (stale) {
        /* No live state: a neutral placeholder instead of a preset the pedal may not be on. */
        s.footswitch_known = false;
        lv_label_set_text(w.preset_label, "");
        lv_label_set_text(w.preset_name, "Preset name");
        layout_preset_row();
    }
    lv_opa_t opa = stale ? LV_OPA_50 : LV_OPA_COVER;
    lv_obj_t *const greyed[] = { w.preset_label, w.preset_name, w.capture, w.ir, w.gate, w.list_btn };
    for (size_t i = 0; i < sizeof(greyed) / sizeof(greyed[0]); i++) lv_obj_set_style_opa(greyed[i], opa, 0);
    for (int i = 0; i < 4; i++) lv_obj_set_style_opa(w.fs_badges[i], opa, 0);
    for (int i = 0; i < NANO_FX_SLOT_COUNT; i++) lv_obj_set_style_opa(w.tiles[i], opa, 0);
}
