#include "block_edits.h"

#include <string.h>

#include "app.h"
#include "esp_log.h"

static const char *TAG = "edits";

static struct {
    int64_t fx_written_us[NANO_FX_SLOT_COUNT], fx_model_written_us[NANO_FX_SLOT_COUNT], gate_written_us, capvol_written_us, ir_written_us;
    uint8_t ir_slot;   /* the IR slot `ir_preset` last showed while on (state field 12): where "on" goes back to */
    int ir_preset;
} s = { .ir_preset = -1 };

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

/* Model (DrD85's FxTypeValue): shown at once from the catalogue; the pedal loads the model's defaults. */
void block_edits_set_fx_model(uint8_t slot, uint32_t type)
{
    const nano_fx_model_t *m = nano_fx_model_by_type(type);
    if (!g_app.link_ready || !g_app.state_valid || slot >= NANO_FX_SLOT_COUNT || !m) return;
    uint8_t f[NANO_FRAME_MAX];
    if (!app_send(f, nano_build_fx_model(f, sizeof(f), slot, type))) return;
    nano_fx_slot_t *fx = &g_app.state.fx[slot];
    ESP_LOGI(TAG, "-> fx slot %u model \"%s\" (was \"%s\")", slot, m->name, fx->model ? fx->model->name : fx->id);
    strlcpy(fx->id, m->id, sizeof(fx->id));
    fx->model = m;
    s.fx_model_written_us[slot] = app_now_us();
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

/* ---- IR: on / off and the pedal's IR list ---------------------------------------- */

static bool ir_writable(void) { return g_app.link_ready && g_app.state_valid; }

/* Where "on" goes: the slot this preset last showed, else the preset's saved IR on the pedal's list, else 1. */
static uint8_t ir_slot_for_on(void)
{
    const nano_state_t *st = &g_app.state;
    if (s.ir_slot && s.ir_preset == st->active_preset) return s.ir_slot;
    const nano_metadata_t *meta = app_meta();
    for (int i = 0; meta && i < meta->ir_count && i < NANO_IR_SLOTS; i++) {
        if (meta->irs[i][0] && strcmp(meta->irs[i], meta->presets[st->active_preset].ir_short_name) == 0) return (uint8_t)(i + 1);
    }
    return 1;
}

/* Selector 3 (as the pedal's encoder scrolls it): slot 1..5, 0 = off. Shown at once with the cached name. */
static void select_ir(uint8_t slot)
{
    uint8_t f[NANO_FRAME_MAX];
    if (!app_send(f, nano_build_cab_select(f, sizeof(f), slot))) return;
    nano_state_t *st = &g_app.state;
    const nano_metadata_t *meta = app_meta();
    if (slot) {
        st->cab_on = true;
        st->cab_slot = slot;
        if (meta && slot <= NANO_IR_SLOTS && meta->irs[slot - 1][0]) strlcpy(st->ir_short_name, meta->irs[slot - 1], sizeof(st->ir_short_name));
        ESP_LOGI(TAG, "-> IR slot %u \"%s\"", slot, st->ir_short_name);
    } else {
        st->cab_on = false;
        ESP_LOGI(TAG, "-> IR off (was slot %u)", s.ir_slot);
    }
    s.ir_written_us = app_now_us();
    ui_mark(UI_STATE);
    link_schedule_state(CONFIRM_MS);
}

void block_edits_set_ir_on(bool on)
{
    if (!ir_writable() || on == g_app.state.cab_on) return;
    select_ir(on ? ir_slot_for_on() : 0);
}

/* The pedal's IR list, wrapping like its encoder (1..5); from the preset's own IR (slot 6) or off, the list's ends. */
void block_edits_step_ir(int delta)
{
    if (!ir_writable() || delta == 0) return;
    const nano_metadata_t *meta = app_meta();
    int count = meta && meta->ir_count ? meta->ir_count : NANO_IR_SLOTS;
    if (count > NANO_IR_SLOTS) count = NANO_IR_SLOTS;
    int cur = g_app.state.cab_on ? g_app.state.cab_slot : ir_slot_for_on();
    int next = cur >= 1 && cur <= count ? (cur - 1 + (delta > 0 ? 1 : count - 1)) % count + 1 : delta > 0 ? 1 : count;
    select_ir((uint8_t)next);
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
        if (s.fx_model_written_us[i] > requested_us && dump->active_preset == shown->active_preset) {
            dump->fx[i] = shown->fx[i];
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
    if (s.ir_written_us > requested_us && dump->active_preset == shown->active_preset) {
        dump->cab_on = shown->cab_on;
        dump->cab_slot = shown->cab_slot;
        strlcpy(dump->ir_short_name, shown->ir_short_name, sizeof(dump->ir_short_name));
        held = true;
    }
    if (dump->cab_on && dump->cab_slot) {
        s.ir_slot = dump->cab_slot;
        s.ir_preset = dump->active_preset;
    }
    if (held) link_schedule_state(CONFIRM_MS); /* one more dump confirms them */
}
