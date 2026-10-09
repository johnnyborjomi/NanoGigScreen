#include "expression.h"

#include <stdio.h>

#include "app.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "nano_ui.h"
#include "preset_select.h"
#include "settings.h"

static const char *TAG = "exp";

#define ASSIGN_TIMEOUT_US (1500 * 1000)
#define ASSIGN_MAX_TRIES 3

static struct {
    int pos;                       /* -1 = unknown on this link */
    int assign_preset;             /* preset whose assignments `assign` holds */
    int req_preset;                /* preset a request is out for */
    int64_t req_us;
    int tries;                     /* unanswered requests for the current preset; stop after a few */
    nano_exp_assignments_t assign;
} s = { .pos = -1, .assign_preset = -1, .req_preset = -1 };

void expression_set_show(bool show)
{
    settings_set_exp_show(show);
    if (show) ui_mark(UI_EXP_POS); /* catch up with what the pedal sent while hidden */
}

void expression_on_preset_change(void)
{
    s.tries = 0;
}

void expression_on_position(const nano_event_t *ev)
{
    s.pos = ev->position;
    if (settings_exp_show()) ui_mark(UI_EXP_POS);
}

/* Paired with every position event; also an empty one after a preset load. */
void expression_on_values(const nano_event_t *ev)
{
    if (!settings_exp_show() || !lvgl_port_lock(20)) return;
    ui_unmark(UI_EXP_CLEAR); /* these are newer */
    nano_ui_set_expression_values(&ev->exp_values);
    lvgl_port_unlock();
}

void expression_on_assignments(const nano_event_t *ev)
{
    int preset = s.req_preset;
    s.req_preset = -1;
    if (preset < 0) {
        ESP_LOGW(TAG, "<- expression assignments without a request; ignored");
        return;
    }
    const nano_exp_assignments_t *a = &ev->exp_assign;
    char desc[160];
    int pos = 0;
    static const char *const SLOT[NANO_FX_SLOT_COUNT] = { "pre1", "pre2", "post1", "post2", "post3" };
    for (int i = 0; i < NANO_FX_SLOT_COUNT && pos < (int)sizeof(desc) - 24; i++) {
        if (a->fx_range[i].assigned) pos += snprintf(desc + pos, sizeof(desc) - pos, " %s %u-%u", SLOT[i], a->fx_range[i].min, a->fx_range[i].max);
        if (a->fx_bypass_mode[i]) pos += snprintf(desc + pos, sizeof(desc) - pos, " %s bypass(m%u)", SLOT[i], a->fx_bypass_mode[i]);
    }
    ESP_LOGI(TAG, "<- expression assignments of preset %d (%d ms):%s%s%s%s", preset + 1, (int)((app_now_us() - s.req_us) / 1000), pos ? desc : " none",
             a->capture_bypass ? " +capture bypass" : "", a->ir_bypass ? " +IR bypass" : "", a->amp_ranges ? " +amp knobs" : "");
    bool same = preset == s.assign_preset;
    s.assign = *a;
    s.assign_preset = preset;
    /* The old preset's values do not belong to these targets. */
    ui_mark(UI_EXP_ASSIGN | (same ? 0 : UI_EXP_CLEAR));
}

static void request_assignments(uint8_t preset)
{
    uint8_t f[NANO_FRAME_MAX];
    if (!app_send(f, nano_build_exp_assign_request(f, sizeof(f), preset))) return;
    s.req_preset = preset;
    s.req_us = app_now_us();
    s.tries++;
    ESP_LOGI(TAG, "-> expression assignments request (preset %u)", preset + 1);
}

/*
 * Read the shown preset's assignments once its select is through (acked and confirmed by a dump; no settle window,
 * it would hold the tile tracks back by 1.5 s after every tap) and they are not the ones we hold. One request at a
 * time; a preset that never answers is asked a few times, then left alone until the preset changes.
 */
void expression_tick(int64_t now)
{
    if (!g_app.link_ready || !g_app.state_valid) return;
    if (s.req_preset >= 0) {
        if (now - s.req_us < ASSIGN_TIMEOUT_US) return;
        ESP_LOGW(TAG, "expression assignments request (preset %d) unanswered", s.req_preset + 1);
        s.req_preset = -1;
    }
    if (preset_select_busy(false) || g_app.state.active_preset == s.assign_preset) return;
    if (s.tries >= ASSIGN_MAX_TRIES) return;
    request_assignments(g_app.state.active_preset);
}

void expression_link_reset(void)
{
    s.assign_preset = s.req_preset = -1;
    s.tries = 0;
    s.pos = -1;
    ui_mark(UI_EXP_POS | UI_EXP_CLEAR | UI_EXP_ASSIGN);
}

void expression_ui_push(uint32_t parts)
{
    if (parts & UI_EXP_CLEAR) nano_ui_set_expression_values(NULL);
    if ((parts & UI_EXP_POS) && (settings_exp_show() || s.pos < 0)) nano_ui_set_expression(s.pos);
    if (parts & UI_EXP_ASSIGN) {
        /* Tile tracks belong to the shown preset: its assignments, or none while they are unknown. */
        bool known = s.assign_preset >= 0 && s.assign_preset == g_app.state.active_preset;
        nano_ui_set_expression_assignments(known ? &s.assign : NULL);
    }
}
