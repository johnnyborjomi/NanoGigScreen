#include "block_edits.h"

#include "app.h"
#include "esp_log.h"

static const char *TAG = "edits";

static struct {
    int64_t fx_written_us[NANO_FX_SLOT_COUNT], gate_written_us, capvol_written_us;
} s;

/* FX block on/off: `0A C0 08 01 18 <slot 4..8> 20 <0 on / 1 off> 1F 00 00 00` (verified 2026-09-12). */
void block_edits_toggle_fx(uint8_t slot, bool currently_on)
{
    if (!g_app.link_ready || !g_app.state_valid || slot >= NANO_FX_SLOT_COUNT) return;
    uint8_t f[NANO_FRAME_MAX];
    if (!app_send(f, nano_build_fx_bypass(f, sizeof(f), slot, !currently_on))) return;
    ESP_LOGI(TAG, "-> fx slot %u %s", slot, currently_on ? "off" : "on");
    g_app.state.fx_on[slot] = !currently_on;
    s.fx_written_us[slot] = app_now_us();
    ui_mark(UI_STATE);
    link_schedule_state(CONFIRM_MS);
}

/* Gate on/off: `0A C0 08 01 18 09 20 <0 on / 1 off> 1F 00 00 00` (verified 2026-09-12). */
void block_edits_toggle_gate(bool currently_on)
{
    if (!g_app.link_ready || !g_app.state_valid) return;
    uint8_t f[NANO_FRAME_MAX];
    if (!app_send(f, nano_build_gate_bypass(f, sizeof(f), !currently_on))) return;
    ESP_LOGI(TAG, "-> gate %s", currently_on ? "off" : "on");
    g_app.state.gate_on = !currently_on;
    s.gate_written_us = app_now_us();
    ui_mark(UI_STATE);
    link_schedule_state(CONFIRM_MS);
}

/* Capture volume, raw 0..255 (Cortex Cloud's write, 2026-10-07). The capture page already shows the value. */
void block_edits_set_capture_volume(uint8_t raw)
{
    if (!g_app.link_ready || !g_app.state_valid) return;
    uint8_t f[NANO_FRAME_MAX];
    if (!app_send(f, nano_build_capture_volume(f, sizeof(f), raw))) return;
    ESP_LOGI(TAG, "-> capture volume %u (%.1f dB)", raw, (double)nano_capture_volume_db(raw));
    g_app.state.capture_volume = raw;
    s.capvol_written_us = app_now_us();
    link_schedule_state(CONFIRM_MS);
}

void block_edits_filter(nano_state_t *dump, int64_t requested_us)
{
    const nano_state_t *shown = &g_app.state;
    bool held = false;
    for (int i = 0; i < NANO_FX_SLOT_COUNT; i++) {
        if (s.fx_written_us[i] > requested_us) {
            dump->fx_on[i] = shown->fx_on[i];
            held = true;
        }
    }
    if (s.gate_written_us > requested_us) {
        dump->gate_on = shown->gate_on;
        held = true;
    }
    if (s.capvol_written_us > requested_us && dump->active_preset == shown->active_preset) {
        dump->capture_volume = shown->capture_volume;
        held = true;
    }
    if (held) link_schedule_state(CONFIRM_MS); /* one more dump confirms them */
}
