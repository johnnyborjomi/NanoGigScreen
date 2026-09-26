/*
 * NanoGig Screen: a standalone Bluetooth gig view for the Neural DSP Nano
 * Cortex on an ESP32-2432S028 (Cheap Yellow Display).
 *
 * Sync rules (from NanoGig's engine, hardware-verified on NanOS 2.2.1):
 *   connect -> state dump (fast, ~0.3 s) -> metadata dump only when the NVS
 *   cache is missing or contradicts the state (the ~17 KB dump takes ~6 s and
 *   queues every footswitch event behind it).
 *   0x1D preset changed -> show the cached name at once, re-read state.
 *   0x1F / 0x1A / 0x1C / 0x73 -> debounced state re-read (400 ms).
 *   tap left / right edge of the name -> c304 preset select, confirmed by the
 *   state dump's field 13.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cyd_board.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nano_assembler.h"
#include "nano_ble.h"
#include "nano_decode.h"
#include "nano_frame.h"
#include "nano_ui.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "nanogig";

#define PACKET_CAP 514              /* MTU 517 - 3 */
#define PACKET_QUEUE_LEN 12
#define ASSEMBLER_CAP (20 * 1024)   /* the metadata dump is ~17 KB */
#define DEBOUNCE_MS 400
#define CONFIRM_MS 300
#define NVS_NAMESPACE "nanogig"
#define NVS_KEY_META "meta"
#define NVS_KEY_BANK "bank"
#define META_MAGIC 0x4E474D31u      /* "NGM1" */

typedef struct {
    uint16_t len;
    uint8_t data[PACKET_CAP];
} packet_t;

typedef enum { MSG_PACKET, MSG_STATUS, MSG_PREV, MSG_NEXT, MSG_TOGGLE_FX, MSG_TUNER, MSG_LINK, MSG_BANK_SIZE, MSG_TOGGLE_GATE, MSG_TUNER_MUTE } msg_kind_t;
typedef struct {
    msg_kind_t kind;
    nano_ble_status_t status;
    char detail[32];
    uint8_t fx_slot;
    bool fx_on;
    bool flag;      /* MSG_TUNER: on, MSG_LINK: connect */
    uint8_t value;  /* MSG_BANK_SIZE */
    packet_t pkt;
} app_msg_t;

typedef struct {
    uint32_t magic;
    nano_metadata_t meta;
} meta_blob_t;

static QueueHandle_t s_queue;
/* Big buffers live on the heap: the ESP32's static DRAM segment is small and NimBLE + LVGL fill it. */
static uint8_t *s_asm_buf;
static nano_metadata_t *s_meta_scratch;
static nano_assembler_t s_asm;
static meta_blob_t s_meta_blob;
static bool s_meta_valid;
static nano_state_t s_state;
static bool s_state_valid;
static bool s_meta_requested_this_link;
static int64_t s_state_due_us;      /* 0 = no pending re-read */
static int64_t s_state_sent_us;     /* when the last state request went out */
static int64_t s_last_pitch_us;     /* last tuner reading; the tuner view clears after silence */
static bool s_tuner_cleared = true;
static bool s_tuner_muted;          /* what we ask for / what the pedal last reported */
static bool s_tuner_view_ours;      /* the view was opened from the menu (we sent tuner-on) */
/* Preset select in flight: the next tap builds on it, a dump that still shows the old preset does not undo it. */
static int s_pending_preset = -1;
static int64_t s_pending_since_us;
#define PENDING_PRESET_TIMEOUT_US (2500 * 1000)
#define TUNER_SILENCE_US (600 * 1000)
#define STATE_MIN_GAP_US (200 * 1000) /* a select's ack events also ask; one dump is enough */
static bool s_link_ready;

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

/* ---- NVS metadata cache -------------------------------------------------- */

static void meta_load(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) return;
    size_t len = sizeof(s_meta_blob);
    esp_err_t err = nvs_get_blob(h, NVS_KEY_META, &s_meta_blob, &len);
    nvs_close(h);
    s_meta_valid = err == ESP_OK && len == sizeof(s_meta_blob) && s_meta_blob.magic == META_MAGIC;
    if (!s_meta_valid) memset(&s_meta_blob, 0, sizeof(s_meta_blob));
    ESP_LOGI(TAG, "metadata cache %s", s_meta_valid ? "loaded" : "empty");
}

static uint8_t bank_load(void)
{
    nvs_handle_t h;
    uint8_t v = 4;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) == ESP_OK) {
        nvs_get_u8(h, NVS_KEY_BANK, &v);
        nvs_close(h);
    }
    return (v >= 2 && v <= 8) ? v : 4;
}

static void nvs_save_u8(const char *key, uint8_t v)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) return;
    uint8_t old = 0xff;
    nvs_get_u8(h, key, &old);
    if (old != v && nvs_set_u8(h, key, v) == ESP_OK) nvs_commit(h);
    nvs_close(h);
}

static void bank_save(uint8_t v) { nvs_save_u8(NVS_KEY_BANK, v); }


static void meta_save(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) return;
    s_meta_blob.magic = META_MAGIC;
    esp_err_t err = nvs_set_blob(h, NVS_KEY_META, &s_meta_blob, sizeof(s_meta_blob));
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    ESP_LOGI(TAG, "metadata cache %s", err == ESP_OK ? "saved" : "save failed");
}

/* ---- BLE callbacks (NimBLE host task): copy and post ---------------------- */

static void ble_on_status(nano_ble_status_t status, const char *detail)
{
    app_msg_t m = { .kind = MSG_STATUS, .status = status };
    strncpy(m.detail, detail ? detail : "", sizeof(m.detail) - 1);
    xQueueSend(s_queue, &m, 0);
}

static void ble_on_notify(const uint8_t *data, size_t len)
{
    if (len > PACKET_CAP) len = PACKET_CAP;
    /* Stack frames of 600 B are fine on the host task; the queue copies by value. */
    static app_msg_t m; /* host task only */
    m.kind = MSG_PACKET;
    m.pkt.len = (uint16_t)len;
    memcpy(m.pkt.data, data, len);
    if (xQueueSend(s_queue, &m, pdMS_TO_TICKS(50)) != pdTRUE) ESP_LOGW(TAG, "packet queue full, dropped %u B", (unsigned)len);
}

static void ui_on_prev(void) { app_msg_t m = { .kind = MSG_PREV }; xQueueSend(s_queue, &m, 0); }
static void ui_on_next(void) { app_msg_t m = { .kind = MSG_NEXT }; xQueueSend(s_queue, &m, 0); }
static void ui_on_toggle_fx(uint8_t slot, bool on)
{
    app_msg_t m = { .kind = MSG_TOGGLE_FX, .fx_slot = slot, .fx_on = on };
    xQueueSend(s_queue, &m, 0);
}
static void ui_on_toggle_gate(bool on) { app_msg_t m = { .kind = MSG_TOGGLE_GATE, .fx_on = on }; xQueueSend(s_queue, &m, 0); }
static void ui_on_tuner_mute(bool mute) { app_msg_t m = { .kind = MSG_TUNER_MUTE, .flag = mute }; xQueueSend(s_queue, &m, 0); }
static void ui_on_tuner(bool on) { app_msg_t m = { .kind = MSG_TUNER, .flag = on }; xQueueSend(s_queue, &m, 0); }
static void ui_on_link(bool connect) { app_msg_t m = { .kind = MSG_LINK, .flag = connect }; xQueueSend(s_queue, &m, 0); }
static void ui_on_bank_size(uint8_t v) { app_msg_t m = { .kind = MSG_BANK_SIZE, .value = v }; xQueueSend(s_queue, &m, 0); }

/* ---- requests ------------------------------------------------------------- */

static void request_state(void)
{
    s_state_due_us = 0;
    int64_t now = esp_timer_get_time();
    if (now - s_state_sent_us < STATE_MIN_GAP_US) return;
    s_state_sent_us = now;
    if (nano_ble_write(NANO_REQ_STATE, sizeof(NANO_REQ_STATE)) == 0) ESP_LOGI(TAG, "-> state request");
}

static void request_metadata(void)
{
    s_meta_requested_this_link = true;
    if (nano_ble_write(NANO_REQ_METADATA, sizeof(NANO_REQ_METADATA)) == 0) ESP_LOGI(TAG, "-> metadata request (~6 s)");
}

static void schedule_state(uint32_t delay_ms)
{
    int64_t due = esp_timer_get_time() + (int64_t)delay_ms * 1000;
    if (s_state_due_us == 0 || due < s_state_due_us) s_state_due_us = due;
}

static bool pending_preset_active(void)
{
    if (s_pending_preset < 0) return false;
    if (esp_timer_get_time() - s_pending_since_us > PENDING_PRESET_TIMEOUT_US) {
        s_pending_preset = -1;
        return false;
    }
    return true;
}

static void select_preset(int delta)
{
    if (!s_link_ready) return;
    int base = pending_preset_active() ? s_pending_preset : (s_state_valid ? s_state.active_preset : 0);
    int idx = (base + delta + NANO_PRESET_COUNT) % NANO_PRESET_COUNT;
    uint8_t frame[NANO_PRESET_SELECT_LEN];
    size_t n = nano_build_preset_select(frame, sizeof(frame), (uint8_t)idx);
    if (n && nano_ble_write(frame, n) == 0) {
        ESP_LOGI(TAG, "-> preset select %d", idx + 1);
        s_pending_preset = idx;
        s_pending_since_us = esp_timer_get_time();
        s_state.active_preset = (uint8_t)idx; /* optimistic; the dump confirms */
        if (lvgl_port_lock(50)) {
            nano_ui_set_preset((uint8_t)idx, s_meta_valid ? &s_meta_blob.meta : NULL);
            lvgl_port_unlock();
        }
        schedule_state(CONFIRM_MS);
    }
}

/* FX block on/off: `0A C0 08 01 18 <slot 4..8> 20 <0 on / 1 off> 1F 00 00 00` (verified 2026-09-12). */
static void toggle_fx(uint8_t slot, bool currently_on)
{
    if (!s_link_ready || !s_state_valid || slot >= NANO_FX_SLOT_COUNT) return;
    uint8_t frame[12];
    size_t n = nano_build_fx_bypass(frame, sizeof(frame), slot, !currently_on);
    if (n && nano_ble_write(frame, n) == 0) {
        ESP_LOGI(TAG, "-> fx slot %u %s", slot, currently_on ? "off" : "on");
        s_state.fx_on[slot] = !currently_on; /* optimistic; the dump confirms */
        if (lvgl_port_lock(50)) {
            nano_ui_set_state(&s_state, s_meta_valid ? &s_meta_blob.meta : NULL);
            lvgl_port_unlock();
        }
        schedule_state(CONFIRM_MS);
    }
}

/* Gate on/off: `0A C0 08 01 18 09 20 <0 on / 1 off> 1F 00 00 00` (verified 2026-09-12). */
static void toggle_gate(bool currently_on)
{
    if (!s_link_ready || !s_state_valid) return;
    uint8_t frame[12];
    size_t n = nano_build_gate_bypass(frame, sizeof(frame), !currently_on);
    if (n && nano_ble_write(frame, n) == 0) {
        ESP_LOGI(TAG, "-> gate %s", currently_on ? "off" : "on");
        s_state.gate_on = !currently_on;
        if (lvgl_port_lock(50)) {
            nano_ui_set_state(&s_state, s_meta_valid ? &s_meta_blob.meta : NULL);
            lvgl_port_unlock();
        }
        schedule_state(CONFIRM_MS);
    }
}

/* Tuner on `0F C0 20 01 2D <f32 Hz> 30 01 38 <mute> 7F 00 00 00` / off `06 C0 20 00 7F 00 00 00` (2026-09-19). */
static void set_tuner(bool on)
{
    if (!s_link_ready) return;
    if (on) {
        float ref = (s_state_valid && s_state.tuner_reference_hz > 0) ? s_state.tuner_reference_hz : 440.0f;
        uint8_t frame[17];
        size_t n = nano_build_tuner_on(frame, sizeof(frame), ref, s_tuner_muted);
        if (n && nano_ble_write(frame, n) == 0) ESP_LOGI(TAG, "-> tuner on (%.1f Hz, %s)", ref, s_tuner_muted ? "muted" : "sound on");
        s_tuner_cleared = true;
        s_tuner_view_ours = true;
    } else if (nano_ble_write(NANO_REQ_TUNER_OFF, sizeof(NANO_REQ_TUNER_OFF)) == 0) {
        ESP_LOGI(TAG, "-> tuner off");
        s_tuner_view_ours = false;
    }
}

/* ---- message handling ----------------------------------------------------- */

static bool cache_contradicts_state(void)
{
    if (!s_meta_valid) return true;
    const nano_preset_record_t *p = &s_meta_blob.meta.presets[s_state.active_preset];
    if (!p->name[0] && !p->capture_name[0]) return s_state.capture_name[0] != 0; /* empty slot on both sides is fine */
    return strcmp(p->capture_name, s_state.capture_name) != 0;
}

static void on_state(const nano_state_t *in)
{
    nano_state_t copy = *in;
    const nano_state_t *st = &copy;
    if (pending_preset_active()) {
        if (in->active_preset == s_pending_preset) {
            s_pending_preset = -1; /* confirmed */
        } else {
            /* A dump for an earlier select; keep showing the target and wait for the next dump. */
            copy.active_preset = (uint8_t)s_pending_preset;
            schedule_state(CONFIRM_MS);
        }
    }
    s_state = *st;
    s_state_valid = true;
    ESP_LOGI(TAG, "<- state: preset %u, capture \"%s\", IR \"%s\", fw %s", st->active_preset + 1, st->capture_name, st->ir_short_name, st->firmware);
    if (lvgl_port_lock(100)) {
        nano_ui_set_state(st, s_meta_valid ? &s_meta_blob.meta : NULL);
        nano_ui_set_stale(false);
        lvgl_port_unlock();
    }
    if (!s_meta_requested_this_link && cache_contradicts_state()) request_metadata();
}

static void on_metadata(const nano_metadata_t *m)
{
    ESP_LOGI(TAG, "<- metadata: %u presets, %u captures, %u IRs", m->preset_record_count, m->capture_count, m->ir_count);
    if (s_state_valid) {
        const nano_preset_record_t *p = &m->presets[s_state.active_preset];
        ESP_LOGI(TAG, "   record %u: name \"%s\" capture \"%s\" ir \"%s\" (state capture \"%s\")", s_state.active_preset + 1, p->name, p->capture_name, p->ir_short_name, s_state.capture_name);
    }
    bool changed = !s_meta_valid || memcmp(&s_meta_blob.meta, m, sizeof(*m)) != 0;
    s_meta_blob.meta = *m;
    s_meta_valid = true;
    if (changed) meta_save();
    /* The state inside the metadata reply is as old as the request: ask for a fresh one. */
    request_state();
}

static void on_message(void *ctx, const uint8_t *body, size_t len, int packets, bool complete)
{
    (void)ctx;
    int msg_type;
    size_t plen = nano_split_trailer(body, len, &msg_type);
    if (packets > 1 || msg_type == NANO_MSG_DUMP) {
        if (!complete) ESP_LOGW(TAG, "unterminated %u-byte message flushed by timeout", (unsigned)len);
        /* Only a reply to our own metadata request can be metadata; the decoder also checks size / records. */
        if (s_meta_requested_this_link && nano_decode_metadata(body, plen, s_meta_scratch)) {
            on_metadata(s_meta_scratch);
            return;
        }
        nano_state_t st;
        if (nano_decode_state(body, len, &st)) {
            on_state(&st);
            return;
        }
        ESP_LOGW(TAG, "dump of %u B did not decode", (unsigned)len);
        return;
    }
    /* Single-packet event: rebuild the 2-byte header the assembler stripped. */
    uint8_t pkt[PACKET_CAP + 2];
    if (len + 2 > sizeof(pkt)) return;
    uint16_t hdr = (uint16_t)(len | NANO_FLAG_START | NANO_FLAG_END);
    pkt[0] = hdr & 0xff;
    pkt[1] = hdr >> 8;
    memcpy(pkt + 2, body, len);
    nano_event_t ev;
    nano_decode_event(pkt, len + 2, &ev);
    if (ev.kind != NANO_EV_TUNER_PITCH && ev.kind != NANO_EV_EXPRESSION && ev.kind != NANO_EV_PRESET_CHANGED && ev.kind != NANO_EV_TAP_TEMPO) {
        /* Everything else is rare: log the bytes so unknown pedal modes (tap tempo, ...) can be mapped. */
        char hex[3 * 40 + 4];
        size_t k = 0;
        for (size_t i = 0; i < len + 2 && i < 40; i++) k += (size_t)snprintf(hex + k, sizeof(hex) - k, "%02X ", pkt[i]);
        ESP_LOGI(TAG, "<- event kind %d type 0x%02X: %s%s", (int)ev.kind, (unsigned)(ev.msg_type < 0 ? 0 : ev.msg_type), hex, len + 2 > 40 ? "..." : "");
    }
    switch (ev.kind) {
    case NANO_EV_PRESET_CHANGED:
        ESP_LOGI(TAG, "<- preset changed: %u", ev.preset + 1);
        s_state.active_preset = ev.preset;
        memcpy(s_state.footswitch, ev.footswitch, 4);
        if (lvgl_port_lock(50)) {
            nano_ui_set_footswitches(ev.footswitch);
            nano_ui_set_preset(ev.preset, s_meta_valid ? &s_meta_blob.meta : NULL);
            lvgl_port_unlock();
        }
        schedule_state(0);
        break;
    case NANO_EV_PRESET_SELECT_ACK:
        schedule_state(0);
        break;
    case NANO_EV_BYPASS_CHANGED:
    case NANO_EV_CONTROL:
        schedule_state(DEBOUNCE_MS);
        break;
    case NANO_EV_TUNER_PITCH:
        s_last_pitch_us = esp_timer_get_time();
        s_tuner_cleared = false;
        if (lvgl_port_lock(20)) {
            if (nano_ui_view() != NANO_VIEW_TUNER) {
                /* Readings with our view closed: the tuner was started on the pedal. Follow it. */
                ESP_LOGI(TAG, "<- pitch while the tuner view is closed: opening it (pedal-started tuner)");
                nano_ui_open_tuner_from_pedal();
                s_tuner_view_ours = false;
            }
            nano_ui_set_tuner(ev.note, ev.cents, ev.in_tune);
            lvgl_port_unlock();
        }
        break;
    case NANO_EV_TUNER_ACK:
        ESP_LOGI(TAG, "<- tuner report: %s, %s, %.1f Hz", ev.tuner_on ? "on" : "off", ev.tuner_muted ? "muted" : "sound on", ev.reference_hz);
        if (lvgl_port_lock(50)) {
            if (ev.tuner_on) {
                if (nano_ui_view() != NANO_VIEW_TUNER) {
                    nano_ui_open_tuner_from_pedal();
                    s_tuner_view_ours = false;
                }
                s_tuner_muted = ev.tuner_muted;
                nano_ui_set_tuner_mute(ev.tuner_muted);
            } else if (nano_ui_view() == NANO_VIEW_TUNER) {
                /* The pedal ended its tuner (footswitch): close the view; nano_ui_show would send tuner-off. */
                s_tuner_view_ours = false;
                nano_ui_open_tuner_from_pedal(); /* no-op if already open; keeps the API symmetrical */
                nano_ui_show(NANO_VIEW_MAIN);
            }
            lvgl_port_unlock();
        }
        break;
    case NANO_EV_TAP_TEMPO:
        /* Live tempo while tapping; on exit the pedal does not always send a change notice, so re-read. */
        s_state.tempo_bpm = ev.tempo_bpm;
        if (lvgl_port_lock(20)) {
            nano_ui_set_tempo(ev.tempo_bpm, ev.tap_active);
            lvgl_port_unlock();
        }
        if (!ev.tap_active) schedule_state(DEBOUNCE_MS);
        break;
    case NANO_EV_EXPRESSION:
        break; /* not shown yet */
    default:
        ESP_LOGD(TAG, "event type 0x%02X (%u B) ignored", (unsigned)ev.msg_type, (unsigned)len);
        break;
    }
}

static void on_status(nano_ble_status_t status, const char *detail)
{
    bool ready = status == NANO_BLE_READY;
    if (ready && !s_link_ready) {
        s_link_ready = true;
        s_meta_requested_this_link = false;
        nano_assembler_reset(&s_asm);
        request_state();
    } else if (!ready && s_link_ready) {
        s_link_ready = false;
        s_state_due_us = 0;
        nano_assembler_reset(&s_asm);
        if (lvgl_port_lock(50)) {
            if (nano_ui_view() == NANO_VIEW_TUNER) nano_ui_show(NANO_VIEW_MAIN);
            lvgl_port_unlock();
        }
    }
    cyd_led(!ready && status != NANO_BLE_CONNECTING, false, status == NANO_BLE_CONNECTING);
    if (lvgl_port_lock(100)) {
        char text[48];
        if (ready && nano_ble_mtu()) {
            snprintf(text, sizeof(text), "Connected, MTU %u", nano_ble_mtu());
        } else {
            strncpy(text, detail, sizeof(text) - 1);
            text[sizeof(text) - 1] = '\0';
        }
        nano_ui_set_status(text, ready);
        nano_ui_set_stale(!ready);
        lvgl_port_unlock();
    }
}

static void app_task(void *arg)
{
    (void)arg;
    app_msg_t m;
    for (;;) {
        if (xQueueReceive(s_queue, &m, pdMS_TO_TICKS(50)) == pdTRUE) {
            switch (m.kind) {
            case MSG_PACKET:
                if (!nano_is_tuner_pitch_packet(m.pkt.data, m.pkt.len)) ESP_LOGD(TAG, "<- %u B", m.pkt.len);
                nano_assembler_push(&s_asm, m.pkt.data, m.pkt.len, now_ms());
                break;
            case MSG_STATUS:
                on_status(m.status, m.detail);
                break;
            case MSG_PREV:
                select_preset(-1);
                break;
            case MSG_NEXT:
                select_preset(+1);
                break;
            case MSG_TOGGLE_FX:
                toggle_fx(m.fx_slot, m.fx_on);
                break;
            case MSG_TUNER:
                set_tuner(m.flag);
                break;
            case MSG_TOGGLE_GATE:
                toggle_gate(m.fx_on);
                break;
            case MSG_TUNER_MUTE:
                s_tuner_muted = m.flag;
                if (lvgl_port_lock(50)) {
                    nano_ui_set_tuner_mute(m.flag); /* optimistic; the pedal's report confirms */
                    lvgl_port_unlock();
                }
                set_tuner(true); /* re-send tuner-on with the new flag, as Cortex Cloud does */
                break;
            case MSG_LINK:
                ESP_LOGI(TAG, "link %s", m.flag ? "enabled" : "disabled");
                nano_ble_set_enabled(m.flag);
                break;
            case MSG_BANK_SIZE:
                bank_save(m.value);
                if (lvgl_port_lock(50)) {
                    nano_ui_set_preset(s_state_valid ? s_state.active_preset : 0, s_meta_valid ? &s_meta_blob.meta : NULL);
                    lvgl_port_unlock();
                }
                break;
            }
        }
        nano_assembler_tick(&s_asm, now_ms());
        if (s_link_ready && s_state_due_us && esp_timer_get_time() >= s_state_due_us) request_state();
        if (!s_tuner_cleared && esp_timer_get_time() - s_last_pitch_us > TUNER_SILENCE_US) {
            s_tuner_cleared = true;
            if (lvgl_port_lock(20)) {
                if (nano_ui_view() == NANO_VIEW_TUNER) nano_ui_set_tuner(NULL, 0, false);
                lvgl_port_unlock();
            }
        }
    }
}

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }
    meta_load();

    lv_display_t *disp = cyd_board_init();
    if (!disp) {
        ESP_LOGE(TAG, "display init failed");
        return;
    }
    nano_ui_callbacks_t ui_cb = {
        .on_prev_preset = ui_on_prev, .on_next_preset = ui_on_next, .on_toggle_fx = ui_on_toggle_fx,
        .on_tuner = ui_on_tuner, .on_link = ui_on_link, .on_bank_size = ui_on_bank_size,
        .on_toggle_gate = ui_on_toggle_gate, .on_tuner_mute = ui_on_tuner_mute,
    };
    if (lvgl_port_lock(0)) {
        nano_ui_create(disp, &ui_cb);
        nano_ui_set_bank_size(bank_load());
        nano_ui_set_status("Starting Bluetooth", false);
        nano_ui_set_stale(true);
        lvgl_port_unlock();
    }
    vTaskDelay(pdMS_TO_TICKS(60)); /* let the first frame flush before lighting the panel */
    cyd_backlight(true);

    s_queue = xQueueCreate(PACKET_QUEUE_LEN, sizeof(app_msg_t));
    s_asm_buf = malloc(ASSEMBLER_CAP);
    s_meta_scratch = malloc(sizeof(*s_meta_scratch));
    if (!s_queue || !s_asm_buf || !s_meta_scratch) {
        ESP_LOGE(TAG, "out of memory at start");
        return;
    }
    nano_assembler_init(&s_asm, s_asm_buf, ASSEMBLER_CAP, on_message, NULL);
    xTaskCreatePinnedToCore(app_task, "nanogig_app", 8192, NULL, 5, NULL, 1);

    nano_ble_callbacks_t ble_cb = { .on_status = ble_on_status, .on_notify = ble_on_notify };
    if (nano_ble_start(&ble_cb) != 0) {
        if (lvgl_port_lock(100)) {
            nano_ui_set_status("Bluetooth failed to start", false);
            lvgl_port_unlock();
        }
    }
}
