#include "tempo.h"

#include "app.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "nano_ui.h"

static const char *TAG = "tempo";

#define WRITE_GAP_US (80 * 1000)
#define SETTLE_US (600 * 1000)
#define HOLD_US (SETTLE_US + 300 * 1000) /* dumps this soon after a press may predate it */
#define BPM_MIN 40.0f
#define BPM_MAX 300.0f

static struct {
    float target;
    bool dirty;                 /* target not written yet */
    int64_t last_press_us, last_write_us;
    bool view_from_pedal;       /* the pedal's tap tempo opened the view; its exit closes it */
} s;

static bool edit_pending(void)
{
    return s.last_press_us && app_now_us() - s.last_press_us < HOLD_US;
}

void tempo_step(int delta)
{
    nano_state_t *st = &g_app.state;
    if (!g_app.link_ready || !g_app.state_valid || st->tempo_bpm <= 0) return;
    float base = edit_pending() ? s.target : st->tempo_bpm;
    float bpm = (float)((int)(base + 0.5f) + delta);
    if (bpm < BPM_MIN) bpm = BPM_MIN;
    if (bpm > BPM_MAX) bpm = BPM_MAX;
    s.target = bpm;
    s.dirty = true;
    s.last_press_us = app_now_us();
    st->tempo_bpm = bpm; /* optimistic; the dump's field 56 confirms */
    if (lvgl_port_lock(UI_LOCK_MS)) { /* a missed frame is replaced by the next press or the dump */
        nano_ui_set_tempo(bpm, false);
        lvgl_port_unlock();
    }
}

void tempo_view(bool open)
{
    s.view_from_pedal = false;
    if (!g_app.link_ready || !g_app.state_valid) return;
    float bpm = g_app.state.tempo_bpm > 0 ? g_app.state.tempo_bpm : 120.0f;
    uint8_t f[NANO_FRAME_MAX];
    size_t n = open ? nano_build_tempo_set(f, sizeof(f), bpm) : nano_build_tempo_exit(f, sizeof(f), bpm);
    if (app_send(f, n)) ESP_LOGI(TAG, "-> tap tempo mode %s (%.0f BPM)", open ? "on" : "off", bpm);
    if (!open) link_schedule_state(CONFIRM_MS);
}

void tempo_on_tap(const nano_event_t *ev)
{
    ESP_LOGI(TAG, "<- tap tempo %s: %.0f BPM", ev->tap_active ? "tap" : "exit", ev->tempo_bpm);
    /* Live tempo while tapping; the first tap opens the tempo view, the pedal's exit closes it.
     * On exit the pedal does not always send a change notice, so re-read. */
    g_app.state.tempo_bpm = ev->tempo_bpm;
    if (lvgl_port_lock(VIEW_LOCK_MS)) {
        if (ev->tap_active && nano_ui_view() != NANO_VIEW_TEMPO) {
            nano_ui_open_tempo_from_pedal();
            s.view_from_pedal = true;
        }
        nano_ui_set_tempo(ev->tempo_bpm, ev->tap_active);
        /* The pedal left the mode itself: close our view without echoing an exit back. */
        if (!ev->tap_active && nano_ui_view() == NANO_VIEW_TEMPO) nano_ui_close_from_pedal();
        lvgl_port_unlock();
    }
    if (!ev->tap_active) {
        s.view_from_pedal = false;
        link_schedule_state(DEBOUNCE_MS);
    }
}

void tempo_filter(nano_state_t *dump)
{
    if (edit_pending()) dump->tempo_bpm = s.target;
}

/* Flush the latest target, then confirm once the presses have settled. */
void tempo_tick(int64_t now)
{
    if (s.dirty && now - s.last_write_us >= WRITE_GAP_US && g_app.link_ready) {
        uint8_t f[NANO_FRAME_MAX];
        if (app_send(f, nano_build_tempo_set(f, sizeof(f), s.target))) ESP_LOGI(TAG, "-> tempo set %.0f", s.target);
        s.dirty = false;
        s.last_write_us = now;
    }
    if (!s.dirty && s.last_press_us && now - s.last_press_us >= SETTLE_US) {
        s.last_press_us = 0;
        link_schedule_state(0);
    }
}

void tempo_link_reset(void)
{
    s.dirty = false;
    s.last_press_us = 0;
    s.view_from_pedal = false;
}
