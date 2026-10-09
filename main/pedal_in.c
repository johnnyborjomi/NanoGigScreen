#include "pedal_in.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app.h"
#include "block_edits.h"
#include "esp_log.h"
#include "expression.h"
#include "ir_library.h"
#include "link.h"
#include "nano_assembler.h"
#include "nano_proto.h"
#include "preset_select.h"
#include "remote_page.h"
#include "rename.h"
#include "settings.h"
#include "tempo.h"
#include "tuner.h"

static const char *TAG = "pedal";

#define ASSEMBLER_CAP (20 * 1024)   /* the metadata dump is ~17 KB */
#define ENCODER_DEBOUNCE_MS 120     /* capture / IR scrolling: the name is shown from the cache at once, the dump confirms */
#define PACKET_MAX 514              /* MTU 517 - 3 */

/* Big buffers live on the heap: the ESP32's static DRAM segment is small and NimBLE + LVGL fill it. */
static uint8_t *s_asm_buf;
static nano_metadata_t *s_meta_scratch;
static nano_assembler_t s_asm;

static uint32_t now_ms(void) { return (uint32_t)(app_now_us() / 1000); }

/* ---- dumps ------------------------------------------------------------------- */

/* One line per dump with every top-level field, to compare dumps taken in different pedal modes. */
static void log_state_fields(const uint8_t *body, size_t len)
{
    char line[400];
    size_t k = 0;
    nano_proto_iter_t it;
    nano_field_t f;
    nano_proto_iter_init(&it, body, len);
    while (nano_proto_next(&it, &f) && k < sizeof(line) - 24) {
        if (f.wire == NANO_WIRE_VARINT) k += (size_t)snprintf(line + k, sizeof(line) - k, "%u=%llu ", (unsigned)f.field, (unsigned long long)f.value);
        else if (f.wire == NANO_WIRE_FIXED32) k += (size_t)snprintf(line + k, sizeof(line) - k, "%u=%.1ff ", (unsigned)f.field, (double)nano_field_f32(&f));
        else k += (size_t)snprintf(line + k, sizeof(line) - k, "%u=[%u] ", (unsigned)f.field, (unsigned)f.len);
    }
    ESP_LOGI(TAG, "   fields: %s", line);
}

static void on_state(const nano_state_t *dump)
{
    int64_t requested_us = link_pop_request_time();
    nano_state_t shown = *dump;
    /* Edits the dump may predate keep what the screen shows; each module asks for the dump that confirms. */
    preset_select_filter(&shown);
    tempo_filter(&shown);
    block_edits_filter(&shown, requested_us);
    if (!g_app.state_valid || g_app.state.active_preset != shown.active_preset) expression_on_preset_change();
    g_app.state = shown;
    g_app.state_valid = true;
    ESP_LOGI(TAG, "<- state: preset %u, capture \"%s\" vol %d (pedal %d), IR \"%s\", %.0f BPM, fw %s", shown.active_preset + 1, shown.capture_name,
             shown.capture_volume, dump->capture_volume, shown.ir_short_name, shown.tempo_bpm, shown.firmware);
    ui_mark(UI_STATE | UI_SYNCED | UI_EXP_ASSIGN);
    remote_pages_on_state();
    link_on_state(dump, preset_select_busy(true));
}

static void on_metadata(const nano_metadata_t *m)
{
    ESP_LOGI(TAG, "<- metadata: %u presets, %u captures, %u IRs", m->preset_record_count, m->capture_count, m->ir_count);
    if (g_app.state_valid) {
        const nano_preset_record_t *p = &m->presets[g_app.state.active_preset];
        ESP_LOGI(TAG, "   record %u: name \"%s\" capture \"%s\" ir \"%s\" (state capture \"%s\")", g_app.state.active_preset + 1, p->name, p->capture_name,
                 p->ir_short_name, g_app.state.capture_name);
    }
    bool changed = !g_app.meta_valid || memcmp(g_app.meta, m, sizeof(*m)) != 0;
    *g_app.meta = *m;
    g_app.meta_valid = true;
    if (changed) settings_save_meta();
    link_on_metadata();
}

/* ---- events ------------------------------------------------------------------ */

/* Capture / IR scrolled on the pedal (0x1C): show the cached name now, confirm with a quick dump. */
static void on_encoder(const nano_event_t *ev)
{
    nano_state_t *st = &g_app.state;
    const nano_metadata_t *meta = app_meta();
    bool shown = false;
    if (ev->selector == 4 && meta && ev->value < NANO_CAPTURE_SLOTS && meta->captures[ev->value][0]) {
        strlcpy(st->capture_name, meta->captures[ev->value], sizeof(st->capture_name));
        st->capture_on = shown = true;
    } else if (ev->selector == 3 && meta && ev->value >= 1 && ev->value <= NANO_IR_SLOTS && meta->irs[ev->value - 1][0]) {
        strlcpy(st->ir_short_name, meta->irs[ev->value - 1], sizeof(st->ir_short_name));
        st->cab_on = shown = true;
    } else if (ev->selector == 3 && ev->value == 0) {
        st->cab_on = false;
        shown = true;
    } else if (ev->selector == 1 && ev->value == 0) {
        st->capture_on = false;
        shown = true;
    }
    ESP_LOGI(TAG, "<- encoder sel %u val %d%s", (unsigned)ev->selector, (int)ev->value, shown ? " (shown from cache)" : "");
    if (shown) ui_mark(UI_STATE);
    link_schedule_state(ENCODER_DEBOUNCE_MS);
}

static void on_event(const nano_event_t *ev)
{
    switch (ev->kind) {
    case NANO_EV_PRESET_CHANGED:
        preset_select_on_changed(ev);
        break;
    case NANO_EV_PRESET_SELECT_ACK:
        preset_select_on_ack();
        break;
    case NANO_EV_BYPASS_CHANGED:
        preset_select_on_bypass_notice();
        break;
    case NANO_EV_CONTROL:
        /* Unsaved-changes flag flipped (EXIT on the pedal reverts the edits): remote pages read again. */
        if (ev->msg_type == NANO_MSG_CHANGED) remote_pages_on_revert();
        if (ev->msg_type == NANO_MSG_ENCODER && ev->value >= 0 && g_app.state_valid) on_encoder(ev);
        else link_schedule_state(DEBOUNCE_MS);
        break;
    case NANO_EV_TUNER_PITCH:
        tuner_on_pitch(ev);
        break;
    case NANO_EV_TUNER_ACK:
        tuner_on_report(ev);
        break;
    case NANO_EV_TAP_TEMPO:
        tempo_on_tap(ev);
        break;
    case NANO_EV_SETTINGS:
        link_on_settings(ev->outputs_muted);
        break;
    case NANO_EV_OUTPUTS_MUTE_ACK:
        link_on_outputs_mute_ack();
        break;
    case NANO_EV_RENAME_REPLY:
        rename_on_reply(ev);
        break;
    case NANO_EV_EXPRESSION:
        expression_on_position(ev);
        break;
    case NANO_EV_EXP_VALUES:
        expression_on_values(ev);
        break;
    case NANO_EV_EXP_ASSIGNMENTS:
        expression_on_assignments(ev);
        break;
    default:
        ESP_LOGD(TAG, "event type 0x%02X ignored", (unsigned)ev->msg_type);
        break;
    }
}

/* Rare events: log the bytes so unknown pedal modes can be mapped (the frequent ones would flood the log). */
static void log_event(const nano_event_t *ev, const uint8_t *pkt, size_t len)
{
    if (ev->kind == NANO_EV_TUNER_PITCH || ev->kind == NANO_EV_EXPRESSION || ev->kind == NANO_EV_PRESET_CHANGED || ev->kind == NANO_EV_TAP_TEMPO) return;
    char hex[3 * 40 + 4];
    size_t k = 0;
    for (size_t i = 0; i < len && i < 40; i++) k += (size_t)snprintf(hex + k, sizeof(hex) - k, "%02X ", pkt[i]);
    ESP_LOGI(TAG, "<- event kind %d type 0x%02X: %s%s", (int)ev->kind, (unsigned)(ev->msg_type < 0 ? 0 : ev->msg_type), hex, len > 40 ? "..." : "");
}

static void on_message(void *ctx, const uint8_t *body, size_t len, int packets, bool complete)
{
    (void)ctx;
    int msg_type;
    size_t plen = nano_split_trailer(body, len, &msg_type);
    if (remote_pages_on_reply(msg_type, body, plen) || ir_library_on_reply(msg_type, body, plen)) return;
    if (packets > 1 || msg_type == NANO_MSG_DUMP) {
        if (!complete) ESP_LOGW(TAG, "unterminated %u-byte message flushed by timeout", (unsigned)len);
        /* Only a reply to our own metadata request can be metadata; the decoder also checks size / records. */
        if (link_metadata_requested() && nano_decode_metadata(body, plen, s_meta_scratch)) {
            on_metadata(s_meta_scratch);
            return;
        }
        nano_state_t st;
        if (nano_decode_state(body, len, &st)) {
            log_state_fields(body, plen);
            on_state(&st);
            return;
        }
        ESP_LOGW(TAG, "dump of %u B did not decode", (unsigned)len);
        return;
    }
    /* Single-packet event: rebuild the 2-byte header the assembler stripped. */
    uint8_t pkt[PACKET_MAX + 2];
    if (len + 2 > sizeof(pkt)) return;
    uint16_t hdr = (uint16_t)(len | NANO_FLAG_START | NANO_FLAG_END);
    pkt[0] = hdr & 0xff;
    pkt[1] = hdr >> 8;
    memcpy(pkt + 2, body, len);
    nano_event_t ev;
    nano_decode_event(pkt, len + 2, &ev);
    log_event(&ev, pkt, len + 2);
    on_event(&ev);
}

/* ---- packets ------------------------------------------------------------------- */

bool pedal_in_init(void)
{
    s_asm_buf = malloc(ASSEMBLER_CAP);
    s_meta_scratch = malloc(sizeof(*s_meta_scratch));
    if (!s_asm_buf || !s_meta_scratch) return false;
    nano_assembler_init(&s_asm, s_asm_buf, ASSEMBLER_CAP, on_message, NULL);
    return true;
}

void pedal_in_packet(const uint8_t *data, size_t len)
{
    if (!nano_is_tuner_pitch_packet(data, len)) ESP_LOGD(TAG, "<- %u B", (unsigned)len);
    if (s_asm_buf) nano_assembler_push(&s_asm, data, len, now_ms());
}

void pedal_in_tick(void)
{
    if (s_asm_buf) nano_assembler_tick(&s_asm, now_ms());
}

void pedal_in_link_reset(void)
{
    if (s_asm_buf) nano_assembler_reset(&s_asm);
}

void pedal_in_release(void)
{
    free(s_asm_buf);
    free(s_meta_scratch);
    s_asm_buf = NULL;
    s_meta_scratch = NULL;
}
