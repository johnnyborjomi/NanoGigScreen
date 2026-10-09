#include "preset_select.h"

#include <string.h>

#include "app.h"
#include "esp_log.h"
#include "expression.h"

static const char *TAG = "preset";

#define PENDING_TIMEOUT_US (2500 * 1000)
#define ACK_TIMEOUT_US (400 * 1000)
#define SETTLE_US (1500 * 1000) /* no metadata re-check this soon after a select: the pedal may still be loading */

static struct {
    int pending;             /* target shown, not yet confirmed by a dump; -1 = none */
    int64_t pending_since_us;
    bool inflight;           /* a select is sent and not acked */
    int sent;                /* the target the in-flight select carries */
    int64_t sent_us, last_us;
} s = { .pending = -1, .sent = -1 };

/* The target, unless it has waited too long for its dump (then the pedal's word stands). */
static bool pending_active(void)
{
    if (s.pending < 0) return false;
    if (app_now_us() - s.pending_since_us > PENDING_TIMEOUT_US) {
        s.pending = -1;
        return false;
    }
    return true;
}

bool preset_select_busy(bool settle)
{
    return pending_active() || s.inflight || (settle && app_now_us() - s.last_us < SETTLE_US);
}

static void send_select(int index)
{
    uint8_t f[NANO_FRAME_MAX];
    if (!app_send(f, nano_build_preset_select(f, sizeof(f), (uint8_t)index))) return;
    ESP_LOGI(TAG, "-> preset select %d", index + 1);
    s.inflight = true;
    s.sent = index;
    s.sent_us = s.last_us = app_now_us();
}

/* The in-flight select was acked (or timed out): send the latest target if it moved on, else confirm. */
static void settled(void)
{
    if (!s.inflight) return;
    s.inflight = false;
    if (pending_active() && s.pending != s.sent) send_select(s.pending);
    else link_schedule_state(0);
}

void preset_select_index(int index)
{
    if (!g_app.link_ready || index < 0 || index >= NANO_PRESET_COUNT) return;
    s.pending = index;
    s.pending_since_us = app_now_us();
    g_app.state.active_preset = (uint8_t)index; /* optimistic; the dump confirms */
    expression_on_preset_change();
    ui_mark(UI_PRESET);
    if (!s.inflight) send_select(index);
    else ESP_LOGI(TAG, "   preset %d held until the ack", index + 1);
}

void preset_select_step(int delta)
{
    if (!g_app.link_ready) return;
    int base = pending_active() ? s.pending : (g_app.state_valid ? g_app.state.active_preset : 0);
    preset_select_index((base + delta + NANO_PRESET_COUNT) % NANO_PRESET_COUNT);
}

void preset_select_filter(nano_state_t *dump)
{
    if (!pending_active()) return;
    if (dump->active_preset == s.pending) {
        s.pending = -1; /* confirmed */
    } else {
        /* A dump for an earlier select; keep showing the target and wait for the next dump. */
        dump->active_preset = (uint8_t)s.pending;
        link_schedule_state(CONFIRM_MS);
    }
}

void preset_select_on_changed(const nano_event_t *ev)
{
    ESP_LOGI(TAG, "<- preset changed: %u", ev->preset + 1);
    g_app.state.active_preset = ev->preset;
    memcpy(g_app.state.footswitch, ev->footswitch, sizeof(g_app.state.footswitch));
    expression_on_preset_change();
    ui_mark(UI_FOOTSWITCHES | UI_PRESET | UI_EXP_ASSIGN);
    link_schedule_state(0);
}

void preset_select_on_ack(void)
{
    if (s.inflight) settled();
    else link_schedule_state(0);
}

void preset_select_on_bypass_notice(void)
{
    /* The pedal's "changed" notice after our select comes ~50 ms before its ack: read the state now (unless a
     * newer target is waiting, which goes out on the ack). A block toggled on the pedal or over MIDI: the notice
     * names nothing, so read right away (the min gap between requests absorbs bursts; a debounce cost ~0.5 s). */
    if (!s.inflight || s.pending == s.sent) link_schedule_state(0);
}

void preset_select_tick(int64_t now)
{
    if (s.inflight && now - s.sent_us > ACK_TIMEOUT_US) {
        ESP_LOGW(TAG, "preset select ack timed out");
        settled();
    }
}

void preset_select_link_reset(void)
{
    s.pending = -1;
    s.inflight = false;
}
