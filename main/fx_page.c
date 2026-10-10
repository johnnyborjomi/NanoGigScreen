#include "fx_page.h"

#include "app.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nano_fx_params.h"
#include "nano_ui.h"
#include "remote_page.h"

static const char *TAG = "fx";

typedef struct {
    int slot;                          /* what the read asked for (from its key) */
    uint32_t type;
    int count;
    float values[NANO_FX_PARAMS_MAX];
} fx_answer_t;

static int s_slot = -1;                /* the editor's slot, -1 = closed */

static uint32_t slot_type(int slot) { return nano_fx_model_type(g_app.state.fx[slot].model); }
static bool slot_on(int slot) { return g_app.state.has_bypass && g_app.state.fx_on[slot] && g_app.state.fx[slot].id[0]; }

/* Slot, on / off and model: a change of any of them asks again. */
static int fx_key(void)
{
    if (s_slot < 0) return -1;
    return s_slot << 20 | (slot_on(s_slot) ? 1 << 19 : 0) | (int)(slot_type(s_slot) & 0x7FFFF);
}

static size_t fx_build_read(uint8_t *out, size_t cap)
{
    /* Never for a bypassed block: DrD85 saw that crash the pedal. Nor for a model without known parameters. */
    if (s_slot < 0 || !slot_on(s_slot) || !nano_fx_def(slot_type(s_slot))) return 0;
    return nano_build_fx_params_request(out, cap, (uint8_t)s_slot);
}

static bool fx_decode(const uint8_t *payload, size_t len, void *data, int preset, int key)
{
    fx_answer_t *a = data;
    a->slot = key < 0 ? -1 : key >> 20;
    a->type = key < 0 ? 0 : (uint32_t)key & 0x7FFFF;
    a->count = nano_decode_fx_params(payload, len, a->values, NANO_FX_PARAMS_MAX);
    const nano_fx_model_t *m = nano_fx_model_by_type(a->type);
    ESP_LOGI(TAG, "<- slot %d \"%s\" (preset %d): %d values, first %.3f %.3f %.3f", a->slot, m ? m->name : "?", preset + 1, a->count,
             (double)(a->count > 0 ? a->values[0] : -1), (double)(a->count > 1 ? a->values[1] : -1), (double)(a->count > 2 ? a->values[2] : -1));
    if (!a->count) ESP_LOG_BUFFER_HEX(TAG, payload, len < 64 ? len : 64);
    return a->count > 0;
}

static fx_answer_t s_answer;

static void fx_show(const void *data, int preset, bool fresh)
{
    nano_ui_set_fx_params(data ? s_answer.values : NULL, s_answer.count, s_answer.slot, s_answer.type, preset, fresh);
}

static const remote_page_ops_t FX_OPS = {
    .name = "FX parameters",
    .reply_type = NANO_MSG_FX_PARAMS,
    .key = fx_key,
    .build_read = fx_build_read,
    .decode = fx_decode,
    .show = fx_show,
};
static remote_page_t s_page = { .ops = &FX_OPS, .data = &s_answer };

void fx_page_init(void)
{
    remote_page_register(&s_page);
}

void fx_page_set_open(uint8_t slot, bool open)
{
    ESP_LOGI(TAG, "editor of slot %u %s, free heap %u B (lowest %u B)", slot, open ? "open" : "closed", (unsigned)esp_get_free_heap_size(),
             (unsigned)esp_get_minimum_free_heap_size());
    if (slot >= NANO_FX_SLOT_COUNT) return;
    s_slot = open ? slot : -1;
    remote_page_set_open(&s_page, open);
}

/* Live while the slider moves; the editor already shows the value. */
void fx_page_set_param(uint8_t slot, uint8_t param, float normalized)
{
    if (!g_app.link_ready || !g_app.state_valid || slot >= NANO_FX_SLOT_COUNT || (int)slot != s_slot || !slot_on(slot)) return;
    const nano_fx_def_t *d = nano_fx_def(slot_type(slot));
    if (!d || param >= d->param_count) return;
    uint8_t f[NANO_FRAME_MAX];
    if (remote_page_write(&s_page, f, nano_build_fx_param(f, sizeof(f), slot, param, normalized), false)) {
        ESP_LOGI(TAG, "-> slot %u %s %.4f", slot, d->params[param].name, (double)normalized);
    }
}
