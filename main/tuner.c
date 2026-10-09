#include "tuner.h"

#include "app.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "nano_ui.h"

static const char *TAG = "tuner";

#define SILENCE_US (600 * 1000)        /* the view clears after this long without a reading */
#define OFF_GRACE_US (900 * 1000)      /* readings still in flight after our tuner-off must not reopen the view */

static struct {
    bool muted;              /* what we ask for / what the pedal last reported */
    bool view_ours;          /* the view was opened from the menu (we sent tuner-on) */
    int64_t off_us;          /* when we last sent tuner-off */
    int64_t last_pitch_us;
    bool cleared;            /* the view shows no note */
} s = { .cleared = true };

void tuner_set(bool on)
{
    if (!g_app.link_ready) return;
    if (on) {
        float ref = g_app.state_valid && g_app.state.tuner_reference_hz > 0 ? g_app.state.tuner_reference_hz : 440.0f;
        uint8_t f[NANO_FRAME_MAX];
        if (app_send(f, nano_build_tuner_on(f, sizeof(f), ref, s.muted))) ESP_LOGI(TAG, "-> tuner on (%.1f Hz, %s)", ref, s.muted ? "muted" : "sound on");
        s.cleared = true;
        s.view_ours = true;
    } else if (app_send(NANO_REQ_TUNER_OFF, sizeof(NANO_REQ_TUNER_OFF))) {
        ESP_LOGI(TAG, "-> tuner off");
        s.view_ours = false;
        s.off_us = app_now_us();
    }
}

void tuner_set_mute(bool mute)
{
    s.muted = mute;
    ui_mark(UI_TUNER_MUTE); /* optimistic; the pedal's report confirms */
    tuner_set(true);
}

void tuner_on_pitch(const nano_event_t *ev)
{
    s.last_pitch_us = app_now_us();
    s.cleared = false;
    if (!lvgl_port_lock(20)) return; /* ~20 readings a second: a missed one is replaced by the next */
    bool open = nano_ui_view() == NANO_VIEW_TUNER;
    /* Readings still streaming right after our tuner-off are not a pedal-started tuner. */
    if (open || s.last_pitch_us - s.off_us >= OFF_GRACE_US) {
        if (!open) {
            /* Readings with our view closed: the tuner was started on the pedal. Follow it. */
            ESP_LOGI(TAG, "<- pitch while the tuner view is closed: opening it (pedal-started tuner)");
            nano_ui_open_tuner_from_pedal();
            s.view_ours = false;
        }
        nano_ui_set_tuner(ev->note, ev->cents, ev->in_tune);
    }
    lvgl_port_unlock();
}

void tuner_on_report(const nano_event_t *ev)
{
    ESP_LOGI(TAG, "<- tuner report: %s, %s, %.1f Hz", ev->tuner_on ? "on" : "off", ev->tuner_muted ? "muted" : "sound on", ev->reference_hz);
    if (ev->tuner_on) s.muted = ev->tuner_muted;
    if (!lvgl_port_lock(VIEW_LOCK_MS)) return;
    if (ev->tuner_on) {
        if (nano_ui_view() != NANO_VIEW_TUNER) {
            /* A footswitch-started tuner (the pedal answers our own tuner-on within ~65 ms, and sends
             * nothing after our tuner-off, capture 2026-09-27), so every on-report is genuine. */
            nano_ui_open_tuner_from_pedal();
            s.view_ours = false;
        }
        nano_ui_set_tuner_mute(ev->tuner_muted);
    } else if (nano_ui_view() == NANO_VIEW_TUNER) {
        /* The pedal ended its tuner (footswitch): close the view without echoing a tuner-off back. */
        s.view_ours = false;
        nano_ui_close_from_pedal();
    }
    lvgl_port_unlock();
}

void tuner_tick(int64_t now)
{
    if (s.cleared || now - s.last_pitch_us <= SILENCE_US || !lvgl_port_lock(UI_LOCK_MS)) return;
    s.cleared = true;
    if (nano_ui_view() == NANO_VIEW_TUNER) nano_ui_set_tuner(NULL, 0, false);
    lvgl_port_unlock();
}

void tuner_link_reset(void)
{
    s.view_ours = false;
}

void tuner_ui_push(uint32_t parts)
{
    if (parts & UI_TUNER_MUTE) nano_ui_set_tuner_mute(s.muted);
}
