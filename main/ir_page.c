#include "ir_page.h"

#include "app.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nano_ui.h"
#include "remote_page.h"

static const char *TAG = "ir";

/* State field 12 while the IR is on (1..5 on the pedal's IR list, 6 seen for most presets, 2026-10-08); 0 = off. */
static int ir_key(void) { return g_app.state.cab_on ? g_app.state.cab_slot : 0; }

static size_t ir_build_read(uint8_t *out, size_t cap)
{
    if (!g_app.state.cab_on) return 0; /* an IR that is off has no settings (the tab says so) */
    /* The reply names the IR it describes, so the log shows whether it is the preset's own. */
    return nano_build_cab_settings_request(out, cap, g_app.state.cab_slot >= 1 ? g_app.state.cab_slot : 1);
}

static bool ir_decode(const uint8_t *payload, size_t len, void *data, int preset, int key)
{
    nano_cab_settings_t *cs = data;
    if (!nano_decode_cab_settings(payload, len, cs)) {
        ESP_LOGW(TAG, "<- IR settings without an IR (%u B)", (unsigned)len);
        ESP_LOG_BUFFER_HEX(TAG, payload, len < 64 ? len : 64);
        return false;
    }
    ESP_LOGI(TAG, "<- IR \"%s\" (%s, kind %u, preset %d slot %d): mic \"%s\" pos %u of %u mics, phase %s; n %.4f %.4f %.4f = %.1f dB, %.0f Hz, %.0f Hz",
             cs->ir_name, cs->factory ? "factory" : "user", (unsigned)cs->kind, preset + 1, key, cs->mic, cs->position + 1, cs->mic_count,
             cs->phase_inverted ? "inverted" : "normal",
             (double)cs->values[0], (double)cs->values[1], (double)cs->values[2], (double)nano_cab_value(NANO_CAB_LEVEL, cs->values[0]),
             (double)nano_cab_value(NANO_CAB_HIGH_PASS, cs->values[1]), (double)nano_cab_value(NANO_CAB_LOW_PASS, cs->values[2]));
    return true;
}

static void ir_show(const void *data, int preset, bool fresh)
{
    nano_ui_set_ir_settings(data, preset, fresh);
}

static const remote_page_ops_t IR_OPS = {
    .name = "IR settings",
    .reply_type = NANO_MSG_CAB_SETTINGS,
    .key = ir_key,
    .build_read = ir_build_read,
    .decode = ir_decode,
    .show = ir_show,
};
static nano_cab_settings_t s_answer;
static remote_page_t s_page = { .ops = &IR_OPS, .data = &s_answer };

void ir_page_init(void)
{
    remote_page_register(&s_page);
}

void ir_page_set_open(bool open)
{
    ESP_LOGI(TAG, "IR tab %s, free heap %u B (lowest %u B)", open ? "open" : "closed", (unsigned)esp_get_free_heap_size(), (unsigned)esp_get_minimum_free_heap_size());
    remote_page_set_open(&s_page, open);
}

static bool writable(void) { return g_app.link_ready && g_app.state_valid && g_app.state.cab_on; }

/* Level / High pass / Low pass: live while the slider moves; the tab already shows the value. */
void ir_page_set_param(nano_cab_param_t param, float normalized)
{
    static const char *const NAMES[NANO_CAB_PARAMS] = { "level", "high pass", "low pass" };
    if (!writable() || param >= NANO_CAB_PARAMS) return;
    uint8_t f[NANO_FRAME_MAX];
    if (remote_page_write(&s_page, f, nano_build_cab_setting(f, sizeof(f), param, normalized), false)) {
        ESP_LOGI(TAG, "-> IR %s %.4f = %.1f", NAMES[param], (double)normalized, (double)nano_cab_value(param, normalized));
    }
}

void ir_page_set_phase(bool inverted)
{
    if (!writable()) return;
    uint8_t f[NANO_FRAME_MAX];
    if (remote_page_write(&s_page, f, nano_build_cab_phase(f, sizeof(f), inverted), true)) {
        ESP_LOGI(TAG, "-> IR phase %s", inverted ? "inverted" : "normal");
    }
}

/* A factory IR's microphone / position: the pedal loads that IR by name, so only while the last answer describes
 * the IR shown now (another preset's IR would be loaded here); a read confirms what it took. */
void ir_page_set_mic(uint8_t position, const char *mic)
{
    const nano_cab_settings_t *cs = remote_page_current(&s_page);
    if (!writable() || position >= NANO_CAB_POSITIONS) return;
    if (!cs || !cs->factory) {
        ESP_LOGW(TAG, "IR mic: no answer for this factory IR yet; ignored");
        return;
    }
    uint8_t f[NANO_FRAME_MAX];
    if (remote_page_write(&s_page, f, nano_build_cab_mic(f, sizeof(f), cs->kind, cs->ir_name, position, mic), true)) {
        ESP_LOGI(TAG, "-> IR \"%s\" mic \"%s\" position %u", cs->ir_name, mic, position + 1);
    }
}
