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
#define META_MAGIC 0x4E474D31u      /* "NGM1" */

typedef struct {
    uint16_t len;
    uint8_t data[PACKET_CAP];
} packet_t;

typedef enum { MSG_PACKET, MSG_STATUS, MSG_PREV, MSG_NEXT, MSG_TOGGLE_FX } msg_kind_t;
typedef struct {
    msg_kind_t kind;
    nano_ble_status_t status;
    char detail[32];
    uint8_t fx_slot;
    bool fx_on;
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

static void select_preset(int delta)
{
    if (!s_link_ready) return;
    int base = s_state_valid ? s_state.active_preset : 0;
    int idx = (base + delta + NANO_PRESET_COUNT) % NANO_PRESET_COUNT;
    uint8_t frame[NANO_PRESET_SELECT_LEN];
    size_t n = nano_build_preset_select(frame, sizeof(frame), (uint8_t)idx);
    if (n && nano_ble_write(frame, n) == 0) {
        ESP_LOGI(TAG, "-> preset select %d", idx + 1);
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

/* ---- message handling ----------------------------------------------------- */

static bool cache_contradicts_state(void)
{
    if (!s_meta_valid) return true;
    const nano_preset_record_t *p = &s_meta_blob.meta.presets[s_state.active_preset];
    if (!p->name[0] && !p->capture_name[0]) return s_state.capture_name[0] != 0; /* empty slot on both sides is fine */
    return strcmp(p->capture_name, s_state.capture_name) != 0;
}

static void on_state(const nano_state_t *st)
{
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
        if (nano_decode_metadata(body, plen, s_meta_scratch)) {
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
    switch (ev.kind) {
    case NANO_EV_PRESET_CHANGED:
        ESP_LOGI(TAG, "<- preset changed: %u", ev.preset + 1);
        s_state.active_preset = ev.preset;
        if (lvgl_port_lock(50)) {
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
            }
        }
        nano_assembler_tick(&s_asm, now_ms());
        if (s_link_ready && s_state_due_us && esp_timer_get_time() >= s_state_due_us) request_state();
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
    nano_ui_callbacks_t ui_cb = { .on_prev_preset = ui_on_prev, .on_next_preset = ui_on_next, .on_toggle_fx = ui_on_toggle_fx };
    if (lvgl_port_lock(0)) {
        nano_ui_create(disp, &ui_cb);
        nano_ui_set_status("Starting Bluetooth", false);
        if (s_meta_valid) nano_ui_set_preset(0, &s_meta_blob.meta);
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
