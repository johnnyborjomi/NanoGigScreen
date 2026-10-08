#include "ui_common.h"

#include <stdio.h>
#include <string.h>

lv_obj_t *ui_label(lv_obj_t *parent, const lv_font_t *font, uint32_t color)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_label_set_text(l, "");
    return l;
}

lv_obj_t *ui_box(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t bg)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(o, lv_color_hex(bg), 0);
    lv_obj_set_scrollable(o, false);
    return o;
}

lv_obj_t *ui_dot(lv_obj_t *parent, int32_t x, int32_t y, int32_t d)
{
    lv_obj_t *o = ui_box(parent, x, y, d, d, C_DIM);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
    return o;
}

lv_obj_t *ui_button(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h, const char *text, const lv_font_t *font, uint32_t bg, uint32_t fg, lv_event_cb_t cb, void *user)
{
    lv_obj_t *b = ui_box(parent, x, y, w, h, bg);
    lv_obj_set_style_radius(b, 10, 0);
    lv_obj_set_clickable(b, true);
    lv_obj_set_style_bg_color(b, lv_color_hex(fg), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(b, LV_OPA_30, LV_STATE_PRESSED);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, user);
    lv_obj_t *l = ui_label(b, font, fg);
    lv_label_set_text(l, text);
    lv_obj_center(l);
    return b;
}

void ui_on_pressed(lv_event_t *e)
{
    lv_indev_t *indev = lv_event_get_indev(e);
    if (!indev) return;
    lv_point_t p;
    lv_indev_get_point(indev, &p);
    printf("touch press x=%d y=%d\n", (int)p.x, (int)p.y);
}

/* The caret: LVGL 9.6 draws no border on a text area's cursor part, so a thin bar of our own sits
 * between the letters, moved after every change and blinking every 500 ms. */
typedef struct {
    lv_obj_t *bar;
    lv_timer_t *timer;
    bool on;
} caret_t;

static void caret_place(lv_obj_t *ta)
{
    caret_t *c = lv_obj_get_user_data(ta);
    if (!c) return;
    lv_obj_t *label = lv_textarea_get_label(ta);
    lv_obj_update_layout(ta);
    lv_point_t p;
    lv_label_get_letter_pos(label, lv_textarea_get_cursor_pos(ta), &p);
    lv_obj_set_pos(c->bar, lv_obj_get_x(label) + p.x - 1, lv_obj_get_y(label) + p.y);
}

static void caret_blink(lv_timer_t *t)
{
    lv_obj_t *ta = lv_timer_get_user_data(t);
    caret_t *c = lv_obj_get_user_data(ta);
    c->on = !c->on;
    caret_place(ta); /* also catches the password dots replacing a just-typed letter */
    lv_obj_set_hidden(c->bar, !c->on);
}

/* After a change or a move: show it at once and start the blink over. */
static void caret_touch(lv_obj_t *ta)
{
    caret_t *c = lv_obj_get_user_data(ta);
    if (!c) return;
    caret_place(ta);
    c->on = true;
    lv_obj_set_hidden(c->bar, false);
    lv_timer_reset(c->timer);
}

static void on_field_event(lv_event_t *e)
{
    lv_obj_t *ta = lv_event_get_target_obj(e);
    if (lv_event_get_code(e) == LV_EVENT_DELETE) {
        caret_t *c = lv_obj_get_user_data(ta);
        lv_timer_delete(c->timer);
        lv_free(c);
        lv_obj_set_user_data(ta, NULL);
        return;
    }
    caret_touch(ta);
}

lv_obj_t *ui_text_field(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h, const lv_font_t *font)
{
    lv_obj_t *ta = lv_textarea_create(parent);
    lv_obj_set_pos(ta, x, y);
    lv_obj_set_size(ta, w, h);
    lv_textarea_set_one_line(ta, true);
    lv_obj_set_style_bg_color(ta, lv_color_hex(C_PANEL), 0);
    lv_obj_set_style_border_color(ta, lv_color_hex(C_ACCENT), 0);
    lv_obj_set_style_text_color(ta, lv_color_hex(C_TEXT), 0);
    lv_obj_set_style_text_font(ta, font, 0);
    lv_obj_set_style_anim_duration(ta, 0, LV_PART_CURSOR | LV_STATE_FOCUSED); /* LVGL's own cursor: off */
    lv_obj_set_style_border_width(ta, 0, LV_PART_CURSOR | LV_STATE_FOCUSED);
    lv_obj_set_style_bg_opa(ta, LV_OPA_TRANSP, LV_PART_CURSOR | LV_STATE_FOCUSED);
    lv_obj_add_state(ta, LV_STATE_FOCUSED);

    caret_t *c = lv_malloc(sizeof(*c));
    if (c) {
        c->bar = ui_box(ta, 0, 0, 2, lv_font_get_line_height(font), C_ACCENT);
        lv_obj_set_clickable(c->bar, false);
        c->on = true;
        c->timer = lv_timer_create(caret_blink, 500, ta);
        lv_obj_set_user_data(ta, c);
        lv_obj_add_event_cb(ta, on_field_event, LV_EVENT_VALUE_CHANGED, NULL);
        lv_obj_add_event_cb(ta, on_field_event, LV_EVENT_RELEASED, NULL); /* a tap in the field moves the cursor */
        lv_obj_add_event_cb(ta, on_field_event, LV_EVENT_DELETE, NULL);
    }
    return ta;
}

/* Keys: letters 10 per row, Backspace and the mode keys wider; the bottom row is mode, arrows, space, OK. */
#define KB_MODE_SYMBOLS2 "#+="
#define KB_MODE_SYMBOLS1 "123"
#define K LV_KEYBOARD_CTRL_BUTTON_FLAGS    /* mode / OK keys: no repeat, act on release */
#define R LV_BUTTONMATRIX_CTRL_CHECKED      /* Backspace and arrows: drawn like K, repeat while held */
static const char *const KB_LOWER[] = {
    "q", "w", "e", "r", "t", "y", "u", "i", "o", "p", "\n",
    "a", "s", "d", "f", "g", "h", "j", "k", "l", "-", "\n",
    "ABC", "z", "x", "c", "v", "b", "n", "m", ".", LV_SYMBOL_BACKSPACE, "\n",
    "1#", LV_SYMBOL_LEFT, " ", LV_SYMBOL_RIGHT, LV_SYMBOL_OK, "" };
static const char *const KB_UPPER[] = {
    "Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P", "\n",
    "A", "S", "D", "F", "G", "H", "J", "K", "L", "-", "\n",
    "abc", "Z", "X", "C", "V", "B", "N", "M", ".", LV_SYMBOL_BACKSPACE, "\n",
    "1#", LV_SYMBOL_LEFT, " ", LV_SYMBOL_RIGHT, LV_SYMBOL_OK, "" };
static const char *const KB_SYMBOLS1[] = {
    "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "\n",
    "-", "_", ".", ",", ":", ";", "!", "?", "@", "#", "\n",
    KB_MODE_SYMBOLS2, "'", "\"", "(", ")", "/", "&", LV_SYMBOL_BACKSPACE, "\n",
    "abc", LV_SYMBOL_LEFT, " ", LV_SYMBOL_RIGHT, LV_SYMBOL_OK, "" };
static const char *const KB_SYMBOLS2[] = {
    "[", "]", "{", "}", "<", ">", "^", "~", "`", "|", "\n",
    "\\", "$", "%", "*", "+", "=", "#", "@", "&", "_", "\n",
    KB_MODE_SYMBOLS1, ",", ".", ":", ";", "!", "?", LV_SYMBOL_BACKSPACE, "\n",
    "abc", LV_SYMBOL_LEFT, " ", LV_SYMBOL_RIGHT, LV_SYMBOL_OK, "" };
static const lv_buttonmatrix_ctrl_t KB_CTRL_LETTERS[] = {
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    K | 3, 2, 2, 2, 2, 2, 2, 2, 2, R | 3,
    K | 3, R | 2, 6, R | 2, K | 3 };
static const lv_buttonmatrix_ctrl_t KB_CTRL_SYMBOLS[] = {
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    K | 3, 2, 2, 2, 2, 2, 2, R | 3,
    K | 3, R | 2, 6, R | 2, K | 3 };
#undef K
#undef R

/* LVGL's handler knows "abc", "ABC" and "1#"; the second symbol page needs its own two keys. */
static void on_keyboard_key(lv_event_t *e)
{
    lv_obj_t *kb = lv_event_get_target_obj(e);
    uint32_t id = lv_keyboard_get_selected_button(kb);
    const char *txt = id == LV_BUTTONMATRIX_BUTTON_NONE ? NULL : lv_keyboard_get_button_text(kb, id);
    if (txt && strcmp(txt, KB_MODE_SYMBOLS2) == 0) lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_USER_1);
    else if (txt && strcmp(txt, KB_MODE_SYMBOLS1) == 0) lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_SPECIAL);
    else lv_keyboard_def_event_cb(e);
    lv_obj_t *ta = lv_keyboard_get_textarea(kb);
    if (ta) caret_touch(ta);
}

lv_obj_t *ui_keyboard(lv_obj_t *parent, int32_t h, lv_obj_t *ta)
{
    lv_obj_t *kb = lv_keyboard_create(parent);
    lv_obj_remove_event_cb(kb, lv_keyboard_def_event_cb);
    lv_obj_add_event_cb(kb, on_keyboard_key, LV_EVENT_VALUE_CHANGED, NULL);
    lv_keyboard_set_map(kb, LV_KEYBOARD_MODE_TEXT_LOWER, KB_LOWER, KB_CTRL_LETTERS);
    lv_keyboard_set_map(kb, LV_KEYBOARD_MODE_TEXT_UPPER, KB_UPPER, KB_CTRL_LETTERS);
    lv_keyboard_set_map(kb, LV_KEYBOARD_MODE_SPECIAL, KB_SYMBOLS1, KB_CTRL_SYMBOLS);
    lv_keyboard_set_map(kb, LV_KEYBOARD_MODE_USER_1, KB_SYMBOLS2, KB_CTRL_SYMBOLS);
    lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_obj_set_size(kb, SCREEN_W, h);
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0); /* bottom-aligned by default: pos would be an offset */
    lv_keyboard_set_textarea(kb, ta);
    lv_obj_set_style_bg_color(kb, lv_color_hex(C_BG), LV_PART_MAIN);
    lv_obj_set_style_pad_all(kb, 2, LV_PART_MAIN);
    lv_obj_set_style_pad_gap(kb, 3, LV_PART_MAIN);
    lv_obj_set_style_bg_color(kb, lv_color_hex(C_PANEL_2), LV_PART_ITEMS);
    lv_obj_set_style_text_color(kb, lv_color_hex(C_TEXT), LV_PART_ITEMS);
    lv_obj_set_style_text_font(kb, &lv_font_montserrat_14, LV_PART_ITEMS);
    lv_obj_set_style_border_width(kb, 0, LV_PART_ITEMS);
    lv_obj_set_style_shadow_width(kb, 0, LV_PART_ITEMS);
    lv_obj_set_style_radius(kb, 5, LV_PART_ITEMS);
    lv_obj_add_event_cb(kb, ui_on_pressed, LV_EVENT_PRESSED, NULL);
    return kb;
}
