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
 *
 * Firmware update (Settings page 3): Bluetooth shuts down, Wi-Fi starts, the
 * screen fetches CONFIG_NANOGIG_OTA_URL into the other OTA slot; closing the
 * page restarts. A new image that never reaches the end of app_main is rolled
 * back by the bootloader.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cyd_board.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nano_assembler.h"
#include "nano_ble.h"
#include "nano_decode.h"
#include "nano_frame.h"
#include "nano_ota.h"
#include "nano_proto.h"
#include "nano_ui.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "nanogig";

#define PACKET_CAP 514              /* MTU 517 - 3 */
#define PACKET_QUEUE_LEN 12
#define ASSEMBLER_CAP (20 * 1024)   /* the metadata dump is ~17 KB */
#define DEBOUNCE_MS 400
#define ENCODER_DEBOUNCE_MS 120     /* capture / IR scrolling: the name is shown from the cache at once, the dump confirms */
#define TUNER_OFF_GRACE_US (900 * 1000)   /* pitch readings still in flight after our tuner-off must not reopen the view */
#define CONFIRM_MS 300
#define NVS_NAMESPACE "nanogig"
#define NVS_KEY_META "meta"
#define NVS_KEY_BANK "bank"
#define NVS_KEY_LABEL_STYLE "lstyle"
#define NVS_KEY_EXP_SHOW "expshow"
#define NVS_KEY_ROTATE "rot"       /* 1 = display turned 180 degrees */
#define NVS_KEY_BRIGHTNESS "bright" /* 1..10 */
#define SETTINGS_READ_DELAY_MS 400  /* after the first state dump / a mute ack: one write at a time on the link */
#define META_MAGIC 0x4E474D31u      /* "NGM1" */

typedef struct {
    uint16_t len;
    uint8_t data[PACKET_CAP];
} packet_t;

typedef enum { MSG_PACKET, MSG_STATUS, MSG_PREV, MSG_NEXT, MSG_TOGGLE_FX, MSG_TUNER, MSG_LINK, MSG_BANK_SIZE, MSG_TOGGLE_GATE, MSG_TUNER_MUTE, MSG_TEMPO_DELTA, MSG_TEMPO_VIEW, MSG_LABEL_STYLE, MSG_OUTPUTS_MUTE, MSG_EXP_SHOW, MSG_ROTATE, MSG_BRIGHTNESS, MSG_UPDATE_OPEN, MSG_UPDATE_CLOSE, MSG_SELECT, MSG_CAPTURE_VOLUME, MSG_RENAME, MSG_IR_VIEW, MSG_CAB_SETTING, MSG_CAB_PHASE, MSG_CAB_MIC } msg_kind_t;
typedef struct {
    msg_kind_t kind;
    nano_ble_status_t status;
    char detail[32];
    uint8_t fx_slot;
    bool fx_on;
    bool flag;      /* MSG_TUNER: on, MSG_LINK: connect, MSG_OUTPUTS_MUTE: mute, MSG_EXP_SHOW: show, MSG_ROTATE: 180 degrees, MSG_IR_VIEW: open */
    int delta;      /* MSG_TEMPO_DELTA */
    uint8_t value;  /* MSG_BANK_SIZE, MSG_LABEL_STYLE, MSG_BRIGHTNESS, MSG_SELECT / MSG_RENAME (preset index), MSG_CAPTURE_VOLUME (raw), MSG_CAB_SETTING (nano_cab_param_t), MSG_CAB_MIC (position) */
    float number;   /* MSG_CAB_SETTING: the pedal's 0..1 */
    char text[NANO_PRESET_NAME_MAX + 1]; /* MSG_RENAME: the new name, MSG_CAB_MIC: the microphone */
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
/* Every state request's send time, oldest first: a dump answers the oldest one, so we know how old it is. */
#define REQ_FIFO_LEN 8
static int64_t s_req_fifo[REQ_FIFO_LEN];
static int s_req_head, s_req_count;
static int64_t s_last_pitch_us;     /* last tuner reading; the tuner view clears after silence */
static bool s_tuner_cleared = true;
static bool s_tuner_muted;          /* what we ask for / what the pedal last reported */
static bool s_tuner_view_ours;      /* the view was opened from the menu (we sent tuner-on) */
static int64_t s_tuner_off_us;      /* when we last sent tuner-off */
/* Preset select in flight: the next tap builds on it, a dump that still shows the old preset does not undo it. */
static int s_pending_preset = -1;
/* Tempo edits are coalesced: instant UI, one write per 80 ms with the latest value, one confirming dump. */
static float s_tempo_target;
static bool s_tempo_dirty;
static int64_t s_tempo_last_press_us, s_tempo_last_write_us;
#define TEMPO_WRITE_GAP_US (80 * 1000)
#define TEMPO_SETTLE_US (600 * 1000)
static bool s_tempo_view_from_pedal; /* the pedal's tap tempo opened the view; its exit closes it */
static int64_t s_pending_since_us;
#define PENDING_PRESET_TIMEOUT_US (2500 * 1000)
/* Rapid taps are coalesced: one select in flight at a time, the latest target goes out on its ack. */
static bool s_select_inflight;
static int s_select_sent = -1;      /* the target the in-flight select carries */
static int64_t s_select_sent_us, s_select_last_us;
#define SELECT_ACK_TIMEOUT_US (400 * 1000)
#define SELECT_SETTLE_US (1500 * 1000) /* no metadata re-check this soon after a select: the pedal may still be loading */
/* Per-tile optimistic state: a dump requested before the tile's last write cannot undo the tap. */
static int64_t s_fx_written_us[NANO_FX_SLOT_COUNT], s_gate_written_us, s_capvol_written_us;
#define TUNER_SILENCE_US (600 * 1000)
#define STATE_MIN_GAP_US (200 * 1000) /* a select's ack events also ask; one dump is enough */
static bool s_link_ready;
/* Device settings (outputs 1/2 mute) are read once per link after the first state dump, and after every mute ack. */
static bool s_settings_read_this_link;
static int64_t s_settings_due_us;   /* 0 = none pending */
/* Expression pedal: ~20 position + values events per second while it moves; the indicators hide a
 * few seconds after the last one. Assignments are per preset and the 0x3D reply carries no preset
 * number, so remember which one was asked for. */
static int s_exp_pos = -1;          /* -1 = unknown on this link */
static bool s_exp_show = true;      /* setting: draw the indicators (off skips the ~40 UI updates/s while the pedal moves) */
static int s_exp_assign_preset = -1; /* preset whose assignments s_exp_assign holds */
static int s_exp_assign_req = -1;    /* preset a request is out for */
static int64_t s_exp_assign_req_us;
static int s_exp_assign_tries;       /* unanswered requests for the current preset; stop after a few */
static nano_exp_assignments_t s_exp_assign;
#define EXP_ASSIGN_TIMEOUT_US (1500 * 1000)
#define EXP_ASSIGN_MAX_TRIES 3

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

static uint8_t label_style_load(void)
{
    nvs_handle_t h;
    uint8_t v = 0;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) == ESP_OK) {
        nvs_get_u8(h, NVS_KEY_LABEL_STYLE, &v);
        nvs_close(h);
    }
    return v <= NANO_LABEL_NUMERIC ? v : 0;
}

/* One u8 setting with a default (missing key or unreadable NVS = the default). */
static uint8_t nvs_load_u8(const char *key, uint8_t dflt)
{
    nvs_handle_t h;
    uint8_t v = dflt;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) == ESP_OK) {
        nvs_get_u8(h, key, &v);
        nvs_close(h);
    }
    return v;
}
static bool rotate_load(void) { return nvs_load_u8(NVS_KEY_ROTATE, cyd_display_rotation() ? 1 : 0) != 0; }
static uint8_t brightness_load(void)
{
    uint8_t v = nvs_load_u8(NVS_KEY_BRIGHTNESS, CYD_BRIGHTNESS_MAX);
    return v < CYD_BRIGHTNESS_MIN ? CYD_BRIGHTNESS_MIN : v > CYD_BRIGHTNESS_MAX ? CYD_BRIGHTNESS_MAX : v;
}

static bool exp_show_load(void)
{
    nvs_handle_t h;
    uint8_t v = 1;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) == ESP_OK) {
        nvs_get_u8(h, NVS_KEY_EXP_SHOW, &v);
        nvs_close(h);
    }
    return v != 0;
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
static void ui_on_select(uint8_t idx) { app_msg_t m = { .kind = MSG_SELECT, .value = idx }; xQueueSend(s_queue, &m, 0); }
static void ui_on_rename(uint8_t idx, const char *name)
{
    app_msg_t m = { .kind = MSG_RENAME, .value = idx };
    snprintf(m.text, sizeof(m.text), "%s", name);
    xQueueSend(s_queue, &m, 0);
}
static void ui_on_capture_volume(uint8_t raw) { app_msg_t m = { .kind = MSG_CAPTURE_VOLUME, .value = raw }; xQueueSend(s_queue, &m, 0); }
static void ui_on_ir_view(bool open) { app_msg_t m = { .kind = MSG_IR_VIEW, .flag = open }; xQueueSend(s_queue, &m, 0); }
static void ui_on_cab_setting(uint8_t param, float n) { app_msg_t m = { .kind = MSG_CAB_SETTING, .value = param, .number = n }; xQueueSend(s_queue, &m, 0); }
static void ui_on_cab_phase(bool inverted) { app_msg_t m = { .kind = MSG_CAB_PHASE, .flag = inverted }; xQueueSend(s_queue, &m, 0); }
static void ui_on_cab_mic(uint8_t position, const char *mic)
{
    app_msg_t m = { .kind = MSG_CAB_MIC, .value = position };
    snprintf(m.text, sizeof(m.text), "%s", mic);
    xQueueSend(s_queue, &m, 0);
}
static void ui_on_toggle_fx(uint8_t slot, bool on)
{
    app_msg_t m = { .kind = MSG_TOGGLE_FX, .fx_slot = slot, .fx_on = on };
    xQueueSend(s_queue, &m, 0);
}
static void ui_on_toggle_gate(bool on) { app_msg_t m = { .kind = MSG_TOGGLE_GATE, .fx_on = on }; xQueueSend(s_queue, &m, 0); }
static void ui_on_tempo_view(bool open) { app_msg_t m = { .kind = MSG_TEMPO_VIEW, .flag = open }; xQueueSend(s_queue, &m, 0); }
static void ui_on_tempo_delta(int d) { app_msg_t m = { .kind = MSG_TEMPO_DELTA, .delta = d }; xQueueSend(s_queue, &m, 0); }
static void ui_on_tuner_mute(bool mute) { app_msg_t m = { .kind = MSG_TUNER_MUTE, .flag = mute }; xQueueSend(s_queue, &m, 0); }
static void ui_on_tuner(bool on) { app_msg_t m = { .kind = MSG_TUNER, .flag = on }; xQueueSend(s_queue, &m, 0); }
static void ui_on_link(bool connect) { app_msg_t m = { .kind = MSG_LINK, .flag = connect }; xQueueSend(s_queue, &m, 0); }
static void ui_on_bank_size(uint8_t v) { app_msg_t m = { .kind = MSG_BANK_SIZE, .value = v }; xQueueSend(s_queue, &m, 0); }
static void ui_on_label_style(uint8_t v) { app_msg_t m = { .kind = MSG_LABEL_STYLE, .value = v }; xQueueSend(s_queue, &m, 0); }
static void ui_on_outputs_mute(bool mute) { app_msg_t m = { .kind = MSG_OUTPUTS_MUTE, .flag = mute }; xQueueSend(s_queue, &m, 0); }
static void ui_on_expression_show(bool show) { app_msg_t m = { .kind = MSG_EXP_SHOW, .flag = show }; xQueueSend(s_queue, &m, 0); }
static void ui_on_rotation(bool rot) { app_msg_t m = { .kind = MSG_ROTATE, .flag = rot }; xQueueSend(s_queue, &m, 0); }
static void ui_on_brightness(uint8_t v) { app_msg_t m = { .kind = MSG_BRIGHTNESS, .value = v }; xQueueSend(s_queue, &m, 0); }
/* Update mode: opening shuts Bluetooth down (blocking, so on the app task); the rest goes straight to the update task. */
static void ui_on_update_open(void) { app_msg_t m = { .kind = MSG_UPDATE_OPEN }; xQueueSend(s_queue, &m, 0); }
static void ui_on_update_close(void) { app_msg_t m = { .kind = MSG_UPDATE_CLOSE }; xQueueSend(s_queue, &m, 0); }
static void ui_on_wifi_scan(void) { nano_ota_scan(); }
static void ui_on_wifi_join(const char *ssid, const char *pass) { nano_ota_join(ssid, pass); }
static void ui_on_update_check(void) { nano_ota_check(); }
static void ui_on_update_install(void) { nano_ota_install(); }

/* ---- firmware update ---------------------------------------------------------- */

static bool s_update_mode;

/* Update task -> update view. */
static void ota_on_event(const nano_ota_event_t *ev)
{
    if (!lvgl_port_lock(200)) return;
    switch (ev->kind) {
    case NANO_OTA_EV_SCANNING:
        nano_ui_update_show_networks(NULL, 0, true);
        break;
    case NANO_OTA_EV_SCAN_DONE: {
        nano_ui_network_t nets[NANO_OTA_MAX_NETWORKS];
        int n = ev->network_count < NANO_OTA_MAX_NETWORKS ? ev->network_count : NANO_OTA_MAX_NETWORKS;
        for (int i = 0; i < n; i++) {
            strlcpy(nets[i].ssid, ev->networks[i].ssid, sizeof(nets[i].ssid));
            nets[i].rssi = ev->networks[i].rssi;
            nets[i].secure = ev->networks[i].secure;
        }
        nano_ui_update_show_networks(nets, n, false);
        break;
    }
    case NANO_OTA_EV_NO_WIFI:
        nano_ui_update_show_networks(NULL, 0, true);
        nano_ota_scan();
        break;
    case NANO_OTA_EV_CONNECTING: {
        char t[64];
        snprintf(t, sizeof(t), "Connecting to %s", ev->text);
        nano_ui_update_status(NANO_UPDATE_BUSY, t, 0);
        break;
    }
    case NANO_OTA_EV_WIFI_SAVED:
        nano_ui_update_set_wifi(ev->text);
        break;
    case NANO_OTA_EV_CHECKING:
        nano_ui_update_status(NANO_UPDATE_BUSY, "Checking for updates", 0);
        break;
    case NANO_OTA_EV_UP_TO_DATE:
        nano_ui_update_status(NANO_UPDATE_UP_TO_DATE, ev->text, 0);
        break;
    case NANO_OTA_EV_AVAILABLE:
        nano_ui_update_status(NANO_UPDATE_AVAILABLE, ev->text, 0);
        break;
    case NANO_OTA_EV_PROGRESS:
        nano_ui_update_status(NANO_UPDATE_DOWNLOADING, NULL, ev->percent);
        break;
    case NANO_OTA_EV_INSTALLED:
        nano_ui_update_status(NANO_UPDATE_DONE, ev->text, 100);
        break;
    case NANO_OTA_EV_ERROR:
        nano_ui_update_status(NANO_UPDATE_ERROR, ev->text, 0);
        break;
    }
    lvgl_port_unlock();
}

static void enter_update_mode(void)
{
    if (s_update_mode) return;
    s_update_mode = true;
    ESP_LOGI(TAG, "update mode: Bluetooth off, Wi-Fi on");
    nano_ble_shutdown();
    /* The pedal is gone until the restart: its buffers (~27 KB) become download headroom. The app loop
     * skips the assembler from here on (packets still queued from the link are dropped). */
    free(s_asm_buf);
    free(s_meta_scratch);
    s_asm_buf = NULL;
    s_meta_scratch = NULL;
    ESP_LOGI(TAG, "pedal buffers released, free heap %u B", (unsigned)esp_get_free_heap_size());
    if (nano_ota_start(ota_on_event) != 0) {
        if (lvgl_port_lock(100)) {
            nano_ui_update_status(NANO_UPDATE_ERROR, "Wi-Fi failed to start", 0);
            lvgl_port_unlock();
        }
        return;
    }
    nano_ota_check(); /* asks for a network first when none is remembered */
}

/* ---- requests ------------------------------------------------------------- */

static void request_state(void)
{
    s_state_due_us = 0;
    int64_t now = esp_timer_get_time();
    if (now - s_state_sent_us < STATE_MIN_GAP_US) {
        s_state_due_us = s_state_sent_us + STATE_MIN_GAP_US; /* not dropped: retried at the end of the gap */
        return;
    }
    s_state_sent_us = now;
    if (nano_ble_write(NANO_REQ_STATE, sizeof(NANO_REQ_STATE)) == 0) {
        ESP_LOGI(TAG, "-> state request");
        if (s_req_count < REQ_FIFO_LEN) s_req_fifo[(s_req_head + s_req_count++) % REQ_FIFO_LEN] = now;
    }
}

/* Send time of the request the arriving dump answers (the oldest outstanding one). */
static int64_t pop_request_time(void)
{
    if (s_req_count == 0) return s_state_sent_us;
    int64_t t = s_req_fifo[s_req_head];
    s_req_head = (s_req_head + 1) % REQ_FIFO_LEN;
    s_req_count--;
    return t;
}

static void request_metadata(void)
{
    s_meta_requested_this_link = true;
    if (nano_ble_write(NANO_REQ_METADATA, sizeof(NANO_REQ_METADATA)) == 0) ESP_LOGI(TAG, "-> metadata request (~6 s)");
}

static void schedule_settings(uint32_t delay_ms)
{
    s_settings_due_us = esp_timer_get_time() + (int64_t)delay_ms * 1000;
}

static void request_settings(void)
{
    s_settings_due_us = 0;
    if (!s_link_ready) return;
    s_settings_read_this_link = true;
    if (nano_ble_write(NANO_REQ_SETTINGS, sizeof(NANO_REQ_SETTINGS)) == 0) ESP_LOGI(TAG, "-> settings request");
}

/* Outputs 1/2 mute: `08 C0 08 01 68 <1/0> 43 00 00 00` (Cortex Cloud's global switch); the 0x44 ack and the
 * settings reply's field 16 confirm. */
static void request_exp_assignments(uint8_t preset)
{
    uint8_t frame[16];
    size_t n = nano_build_exp_assign_request(frame, sizeof(frame), preset);
    if (n && nano_ble_write(frame, n) == 0) {
        s_exp_assign_req = preset;
        s_exp_assign_req_us = esp_timer_get_time();
        s_exp_assign_tries++;
        ESP_LOGI(TAG, "-> expression assignments request (preset %u)", preset + 1);
    }
}

static bool pending_preset_active(void);

/*
 * Assignments are per preset (Cortex Cloud's 0x3C): read them once the shown preset has settled
 * (no select in flight, past the settle window) and they are not the ones we hold. One request at a
 * time; a preset that never answers is asked a few times, then left alone until the preset changes.
 */
static void exp_assign_tick(void)
{
    if (!s_link_ready || !s_state_valid) return;
    int64_t now = esp_timer_get_time();
    if (s_exp_assign_req >= 0) {
        if (now - s_exp_assign_req_us < EXP_ASSIGN_TIMEOUT_US) return;
        ESP_LOGW(TAG, "expression assignments request (preset %d) unanswered", s_exp_assign_req + 1);
        s_exp_assign_req = -1;
    }
    /* Only the select itself has to be through (acked and confirmed by a dump); no settle window here,
     * it would hold the tile tracks back by 1.5 s after every tap. */
    bool switching = pending_preset_active() || s_select_inflight;
    if (switching || s_state.active_preset == s_exp_assign_preset) return;
    if (s_exp_assign_tries >= EXP_ASSIGN_MAX_TRIES) return;
    request_exp_assignments(s_state.active_preset);
}

/* Tile tracks belong to the shown preset: push its assignments, or none while they are unknown. */
static void exp_push_assignments(uint8_t shown_preset)
{
    bool known = s_exp_assign_preset >= 0 && s_exp_assign_preset == shown_preset;
    if (lvgl_port_lock(50)) {
        nano_ui_set_expression_assignments(known ? &s_exp_assign : NULL);
        lvgl_port_unlock();
    }
}

static void exp_reset(void)
{
    s_exp_pos = -1;
    if (lvgl_port_lock(50)) {
        nano_ui_set_expression(-1);
        nano_ui_set_expression_values(NULL);
        lvgl_port_unlock();
    }
}

static void set_outputs_mute(bool mute)
{
    if (!s_link_ready) return;
    uint8_t frame[10];
    size_t n = nano_build_outputs_mute(frame, sizeof(frame), mute);
    if (n && nano_ble_write(frame, n) == 0) ESP_LOGI(TAG, "-> outputs 1/2 %s", mute ? "mute" : "on");
    schedule_settings(SETTINGS_READ_DELAY_MS);
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

static void send_select(int idx)
{
    uint8_t frame[NANO_PRESET_SELECT_LEN];
    size_t n = nano_build_preset_select(frame, sizeof(frame), (uint8_t)idx);
    if (n && nano_ble_write(frame, n) == 0) {
        ESP_LOGI(TAG, "-> preset select %d", idx + 1);
        s_select_inflight = true;
        s_select_sent = idx;
        s_select_sent_us = s_select_last_us = esp_timer_get_time();
    }
}

/* The in-flight select was acked (or timed out): send the latest target if it moved on, else confirm. */
static void select_settled(void)
{
    if (!s_select_inflight) return;
    s_select_inflight = false;
    if (pending_preset_active() && s_pending_preset != s_select_sent) send_select(s_pending_preset);
    else schedule_state(0);
}

/* Show the target at once; send it now, or hold it while an earlier select is still unacked
 * (the pedal would load every preset skipped past, ~150 ms each). */
static void select_preset_index(int idx)
{
    if (!s_link_ready || idx < 0 || idx >= NANO_PRESET_COUNT) return;
    s_pending_preset = idx;
    s_pending_since_us = esp_timer_get_time();
    s_state.active_preset = (uint8_t)idx; /* optimistic; the dump confirms */
    s_exp_assign_tries = 0;
    if (lvgl_port_lock(50)) {
        nano_ui_set_preset((uint8_t)idx, s_meta_valid ? &s_meta_blob.meta : NULL);
        lvgl_port_unlock();
    }
    if (!s_select_inflight) send_select(idx);
    else ESP_LOGI(TAG, "   preset %d held until the ack", idx + 1);
}

/* prev / next: step from the target still in flight, if any. */
static void select_preset(int delta)
{
    if (!s_link_ready) return;
    int base = pending_preset_active() ? s_pending_preset : (s_state_valid ? s_state.active_preset : 0);
    select_preset_index((base + delta + NANO_PRESET_COUNT) % NANO_PRESET_COUNT);
}

/* FX block on/off: `0A C0 08 01 18 <slot 4..8> 20 <0 on / 1 off> 1F 00 00 00` (verified 2026-09-12). */
static void toggle_fx(uint8_t slot, bool currently_on)
{
    if (!s_link_ready || !s_state_valid || slot >= NANO_FX_SLOT_COUNT) return;
    uint8_t frame[12];
    size_t n = nano_build_fx_bypass(frame, sizeof(frame), slot, !currently_on);
    if (n && nano_ble_write(frame, n) == 0) {
        ESP_LOGI(TAG, "-> fx slot %u %s", slot, currently_on ? "off" : "on");
        s_state.fx_on[slot] = !currently_on; /* optimistic; a dump requested after this write confirms */
        s_fx_written_us[slot] = esp_timer_get_time();
        if (lvgl_port_lock(50)) {
            nano_ui_set_state(&s_state, s_meta_valid ? &s_meta_blob.meta : NULL);
            lvgl_port_unlock();
        }
        schedule_state(CONFIRM_MS);
    }
}

/* Capture volume, raw 0..255 (Cortex Cloud's write, 2026-10-07). A live edit: the preset is not saved.
 * The capture page already shows the value; a dump requested after this write confirms it. */
static void set_capture_volume(uint8_t raw)
{
    if (!s_link_ready || !s_state_valid) return;
    uint8_t frame[16];
    size_t n = nano_build_capture_volume(frame, sizeof(frame), raw);
    if (n && nano_ble_write(frame, n) == 0) {
        ESP_LOGI(TAG, "-> capture volume %u (%.1f dB)", raw, (double)nano_capture_volume_db(raw));
        s_state.capture_volume = raw;
        s_capvol_written_us = esp_timer_get_time();
        schedule_state(CONFIRM_MS);
    }
}

/* IR settings (Level, High pass, Low pass; frames from DrD85, verified 2026-10-08). The state dumps do not carry
 * them: they are read while the IR tab shows, on open and again for another preset or IR slot. */
static bool s_ir_view;
static int s_ir_read_preset = -1, s_ir_read_slot = -1; /* what the last read asked about */
static int64_t s_ir_read_us, s_ir_written_us;              /* last read sent, last setting written */
static nano_cab_settings_t s_ir_last;                      /* the last answer (a microphone write names its IR) */
static bool s_ir_last_valid;
static bool s_ir_ui_pending, s_ir_ui_fresh;                /* s_ir_last still to be shown (the display was busy) */

/* Hand the last answer to the IR tab; retried from the loop while the display is busy (a dropped answer left
 * the tab on edits that EXIT had reverted, 2026-10-09). */
static void ir_push_ui(void)
{
    if (!s_ir_ui_pending) return;
    if (!s_ir_view) {
        s_ir_ui_pending = false;
        return;
    }
    if (!lvgl_port_lock(50)) return;
    nano_ui_set_ir_settings(&s_ir_last, s_ir_read_preset, s_ir_ui_fresh);
    lvgl_port_unlock();
    s_ir_ui_pending = false;
}

static void request_ir_settings(void)
{
    if (!s_link_ready || !s_state_valid) return;
    s_ir_read_preset = s_state.active_preset;
    s_ir_read_slot = s_state.cab_on ? s_state.cab_slot : 0;
    if (!s_state.cab_on) return; /* an IR that is off has no settings to show (the tab says so) */
    /* State field 12 as the slot: 1..5 on the pedal's IR list, 6 seen for most presets (2026-10-08). The reply
     * names the IR it describes, so the log shows whether it is the preset's own. */
    uint8_t slot = s_state.cab_slot >= 1 ? s_state.cab_slot : 1;
    uint8_t frame[16];
    size_t n = nano_build_cab_settings_request(frame, sizeof(frame), slot);
    if (n && nano_ble_write(frame, n) == 0) {
        s_ir_read_us = esp_timer_get_time();
        ESP_LOGI(TAG, "-> IR settings request (preset %u, slot %u)", s_state.active_preset + 1, slot);
    }
}

static void ir_settings_reply(const uint8_t *payload, size_t len)
{
    nano_cab_settings_t *cs = &s_ir_last;
    if (!nano_decode_cab_settings(payload, len, cs)) {
        s_ir_last_valid = false;
        ESP_LOGW(TAG, "<- IR settings without an IR (%u B)", (unsigned)len);
        ESP_LOG_BUFFER_HEX(TAG, payload, len < 64 ? len : 64);
        return;
    }
    s_ir_last_valid = true;
    ESP_LOGI(TAG, "<- IR \"%s\" (%s, kind %u, slot %u): mic \"%s\" pos %u of %u mics, phase %s; n %.4f %.4f %.4f = %.1f dB, %.0f Hz, %.0f Hz",
             cs->ir_name, cs->factory ? "factory" : "user", (unsigned)cs->kind, s_state.cab_slot, cs->mic, cs->position + 1, cs->mic_count,
             cs->phase_inverted ? "inverted" : "normal",
             (double)cs->values[0], (double)cs->values[1], (double)cs->values[2], (double)nano_cab_value(NANO_CAB_LEVEL, cs->values[0]),
             (double)nano_cab_value(NANO_CAB_HIGH_PASS, cs->values[1]), (double)nano_cab_value(NANO_CAB_LOW_PASS, cs->values[2]));
    /* Asked after our last write: the pedal's word stands (EXIT reverts edits), even right after a touch. */
    s_ir_ui_fresh = s_ir_read_us > s_ir_written_us;
    s_ir_ui_pending = true;
    ir_push_ui();
}

static void set_cab_phase(bool inverted)
{
    if (!s_link_ready || !s_state_valid || !s_state.cab_on) return;
    uint8_t frame[8];
    if (nano_build_cab_phase(frame, sizeof(frame), inverted) && nano_ble_write(frame, sizeof(frame)) == 0) {
        s_ir_written_us = esp_timer_get_time();
        ESP_LOGI(TAG, "-> IR phase %s", inverted ? "inverted" : "normal");
        request_ir_settings(); /* confirms it */
    }
}

/* A factory IR's microphone / position: the pedal loads that IR; a read confirms what it took. */
static void set_cab_mic(uint8_t position, const char *mic)
{
    if (!s_link_ready || !s_state_valid || !s_state.cab_on || !s_ir_last_valid || !s_ir_last.factory) return;
    uint8_t frame[128];
    size_t n = nano_build_cab_mic(frame, sizeof(frame), s_ir_last.kind, s_ir_last.ir_name, position, mic);
    if (n && nano_ble_write(frame, n) == 0) {
        ESP_LOGI(TAG, "-> IR \"%s\" mic \"%s\" position %u", s_ir_last.ir_name, mic, position + 1);
        request_ir_settings();
    }
}

static void set_cab_setting(nano_cab_param_t param, float normalized)
{
    if (!s_link_ready || !s_state_valid || !s_state.cab_on) return;
    uint8_t frame[16];
    size_t n = nano_build_cab_setting(frame, sizeof(frame), param, normalized);
    static const char *const names[NANO_CAB_PARAMS] = { "level", "high pass", "low pass" };
    if (n && nano_ble_write(frame, n) == 0) {
        s_ir_written_us = esp_timer_get_time();
        ESP_LOGI(TAG, "-> IR %s %.4f = %.1f", names[param], (double)normalized, (double)nano_cab_value(param, normalized));
    }
}

/* Preset rename (RenamePreset, verified 2026-10-08): the pedal stores the name at once and answers
 * type 0x70. The cached name changes only when it says yes. */
#define RENAME_TIMEOUT_US 3000000
static int s_rename_idx = -1;         /* waiting for the answer for this preset */
static char s_rename_name[NANO_PRESET_NAME_MAX + 1];
static int64_t s_rename_sent_us;

static void rename_answer(bool ok, const char *msg)
{
    int idx = s_rename_idx;
    s_rename_idx = -1;
    if (idx < 0) return;
    if (ok && s_meta_valid) {
        snprintf(s_meta_blob.meta.presets[idx].name, sizeof(s_meta_blob.meta.presets[idx].name), "%s", s_rename_name);
        meta_save();
    }
    if (lvgl_port_lock(50)) {
        if (ok) nano_ui_set_preset(s_state_valid ? s_state.active_preset : 0, s_meta_valid ? &s_meta_blob.meta : NULL);
        nano_ui_rename_result((uint8_t)idx, ok, msg);
        lvgl_port_unlock();
    }
}

static void rename_preset(uint8_t idx, const char *name)
{
    uint8_t frame[48];
    size_t n = nano_build_preset_rename(frame, sizeof(frame), idx, name);
    if (!s_link_ready || !n || nano_ble_write(frame, n) != 0) {
        s_rename_idx = idx;
        rename_answer(false, "Not connected to the pedal");
        return;
    }
    ESP_LOGI(TAG, "-> rename preset %u to \"%s\"", idx + 1, name);
    s_rename_idx = idx;
    snprintf(s_rename_name, sizeof(s_rename_name), "%s", name);
    s_rename_sent_us = esp_timer_get_time();
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
        s_gate_written_us = esp_timer_get_time();
        if (lvgl_port_lock(50)) {
            nano_ui_set_state(&s_state, s_meta_valid ? &s_meta_blob.meta : NULL);
            lvgl_port_unlock();
        }
        schedule_state(CONFIRM_MS);
    }
}

static bool tempo_edit_pending(void)
{
    return s_tempo_last_press_us && esp_timer_get_time() - s_tempo_last_press_us < TEMPO_SETTLE_US + 300 * 1000;
}

/* Tempo set by mirroring the pedal's 0x91 per-tap message (verified 2026-09-26); the state re-read confirms. */
static void step_tempo(int delta)
{
    if (!s_link_ready || !s_state_valid || s_state.tempo_bpm <= 0) return;
    float base = tempo_edit_pending() ? s_tempo_target : s_state.tempo_bpm;
    float bpm = (float)((int)(base + 0.5f) + delta);
    if (bpm < 40.0f) bpm = 40.0f;
    if (bpm > 300.0f) bpm = 300.0f;
    s_tempo_target = bpm;
    s_tempo_dirty = true;
    s_tempo_last_press_us = esp_timer_get_time();
    s_state.tempo_bpm = bpm; /* optimistic; the dump's field 56 confirms */
    if (lvgl_port_lock(20)) {
        nano_ui_set_tempo(bpm, false);
        lvgl_port_unlock();
    }
}

/* Tempo view opened / closed on the screen: put the pedal in / out of its tap tempo mode. */
static void set_tap_mode(bool on)
{
    if (!s_link_ready || !s_state_valid) return;
    float bpm = s_state.tempo_bpm > 0 ? s_state.tempo_bpm : 120.0f;
    uint8_t frame[15];
    size_t n = on ? nano_build_tempo_set(frame, sizeof(frame), bpm) : nano_build_tempo_exit(frame, sizeof(frame), bpm);
    if (n && nano_ble_write(frame, n) == 0) ESP_LOGI(TAG, "-> tap tempo mode %s (%.0f BPM)", on ? "on" : "off", bpm);
    if (!on) schedule_state(CONFIRM_MS);
}

/* Called from the app loop: flush the latest target, then confirm once the presses have settled. */
static void tempo_edit_tick(void)
{
    int64_t now = esp_timer_get_time();
    if (s_tempo_dirty && now - s_tempo_last_write_us >= TEMPO_WRITE_GAP_US && s_link_ready) {
        uint8_t frame[15];
        size_t n = nano_build_tempo_set(frame, sizeof(frame), s_tempo_target);
        if (n && nano_ble_write(frame, n) == 0) ESP_LOGI(TAG, "-> tempo set %.0f", s_tempo_target);
        s_tempo_dirty = false;
        s_tempo_last_write_us = now;
    }
    if (!s_tempo_dirty && s_tempo_last_press_us && now - s_tempo_last_press_us >= TEMPO_SETTLE_US) {
        s_tempo_last_press_us = 0;
        schedule_state(0);
    }
}

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
        else if (f.wire == NANO_WIRE_FIXED32) {
            float v; memcpy(&v, f.raw, 4);
            k += (size_t)snprintf(line + k, sizeof(line) - k, "%u=%.1ff ", (unsigned)f.field, v);
        } else k += (size_t)snprintf(line + k, sizeof(line) - k, "%u=[%u] ", (unsigned)f.field, (unsigned)f.len);
    }
    ESP_LOGI(TAG, "   fields: %s", line);
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
        s_tuner_off_us = esp_timer_get_time();
    }
}

/* ---- message handling ----------------------------------------------------- */

/* Compare the dump's own preset against its cache record (never a pending target against another preset's names). */
static bool cache_contradicts_state(const nano_state_t *in)
{
    if (!s_meta_valid) return true;
    const nano_preset_record_t *p = &s_meta_blob.meta.presets[in->active_preset];
    if (!p->name[0] && !p->capture_name[0]) return in->capture_name[0] != 0; /* empty slot on both sides is fine */
    return strcmp(p->capture_name, in->capture_name) != 0;
}

static void on_state(const nano_state_t *in)
{
    int64_t requested_us = pop_request_time();
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
    if (tempo_edit_pending()) copy.tempo_bpm = s_tempo_target; /* a dump from before the last press */
    /* Tiles written after this dump was requested keep their tapped state; one more dump confirms them. */
    bool stale_tile = false;
    for (int i = 0; i < NANO_FX_SLOT_COUNT; i++) {
        if (s_fx_written_us[i] > requested_us) { copy.fx_on[i] = s_state.fx_on[i]; stale_tile = true; }
    }
    if (s_gate_written_us > requested_us) { copy.gate_on = s_state.gate_on; stale_tile = true; }
    if (s_capvol_written_us > requested_us && copy.active_preset == s_state.active_preset) { copy.capture_volume = s_state.capture_volume; stale_tile = true; }
    if (stale_tile) schedule_state(CONFIRM_MS);
    if (!s_state_valid || s_state.active_preset != st->active_preset) s_exp_assign_tries = 0;
    s_state = *st;
    s_state_valid = true;
    ESP_LOGI(TAG, "<- state: preset %u, capture \"%s\" vol %d (pedal %d), IR \"%s\", %.0f BPM, fw %s", st->active_preset + 1, st->capture_name, st->capture_volume, in->capture_volume, st->ir_short_name, st->tempo_bpm, st->firmware);
    if (lvgl_port_lock(100)) {
        nano_ui_set_state(st, s_meta_valid ? &s_meta_blob.meta : NULL);
        nano_ui_set_stale(false);
        nano_ui_set_connected(true); /* the connect page closes only once the pedal's state is on screen */
        lvgl_port_unlock();
    }
    exp_push_assignments(st->active_preset);
    /* IR tab open: another preset or IR (or the IR switched on / off) needs its settings read. */
    if (s_ir_view && (st->active_preset != s_ir_read_preset || (st->cab_on ? st->cab_slot : 0) != s_ir_read_slot)) request_ir_settings();
    bool switching = pending_preset_active() || s_select_inflight || esp_timer_get_time() - s_select_last_us < SELECT_SETTLE_US;
    if (!s_meta_requested_this_link && !switching && cache_contradicts_state(in)) request_metadata();
    else if (!s_settings_read_this_link && !s_settings_due_us) schedule_settings(SETTINGS_READ_DELAY_MS);
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
    if (!s_settings_read_this_link) schedule_settings(SETTINGS_READ_DELAY_MS);
}

static void on_message(void *ctx, const uint8_t *body, size_t len, int packets, bool complete)
{
    (void)ctx;
    int msg_type;
    size_t plen = nano_split_trailer(body, len, &msg_type);
    if (msg_type == NANO_MSG_CAB_SETTINGS) {
        ir_settings_reply(body, plen);
        return;
    }
    if (packets > 1 || msg_type == NANO_MSG_DUMP) {
        if (!complete) ESP_LOGW(TAG, "unterminated %u-byte message flushed by timeout", (unsigned)len);
        /* Only a reply to our own metadata request can be metadata; the decoder also checks size / records. */
        if (s_meta_requested_this_link && nano_decode_metadata(body, plen, s_meta_scratch)) {
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
        exp_push_assignments(ev.preset);
        s_exp_assign_tries = 0;
        schedule_state(0);
        break;
    case NANO_EV_PRESET_SELECT_ACK:
        if (s_select_inflight) select_settled();
        else schedule_state(0);
        break;
    case NANO_EV_BYPASS_CHANGED:
        /* The pedal's "changed" notice after our select comes ~50 ms before its ack: read the state now
         * (unless a newer target is waiting, which goes out on the ack). */
        if (s_select_inflight && s_pending_preset == s_select_sent) schedule_state(0);
        /* A block toggled on the pedal or over MIDI: the notice names nothing, so read the state right
         * away (the min gap between requests absorbs bursts; no debounce, it cost ~0.5 s per toggle). */
        else if (!s_select_inflight) schedule_state(0);
        break;
    case NANO_EV_CONTROL:
        /* Unsaved-changes flag flipped (EXIT on the pedal reverts the edits): the IR settings may be back, and the
         * IR slot with them; the state read below decides which slot to ask about (on_state). */
        if (ev.msg_type == NANO_MSG_CHANGED && s_ir_view) s_ir_read_slot = -1;
        if (ev.msg_type == NANO_MSG_ENCODER && ev.value >= 0 && s_state_valid) {
            /* Capture / IR scrolled on the pedal: show the cached name now, confirm with a quick dump. */
            const nano_metadata_t *meta = s_meta_valid ? &s_meta_blob.meta : NULL;
            bool shown = false;
            if (ev.selector == 4 && meta && ev.value < NANO_CAPTURE_SLOTS && meta->captures[ev.value][0]) {
                strncpy(s_state.capture_name, meta->captures[ev.value], sizeof(s_state.capture_name) - 1);
                s_state.capture_on = true;
                shown = true;
            } else if (ev.selector == 3 && meta && ev.value >= 1 && ev.value <= NANO_IR_SLOTS && meta->irs[ev.value - 1][0]) {
                strncpy(s_state.ir_short_name, meta->irs[ev.value - 1], sizeof(s_state.ir_short_name) - 1);
                s_state.cab_on = true;
                shown = true;
            } else if (ev.selector == 3 && ev.value == 0) {
                s_state.cab_on = false;
                shown = true;
            } else if (ev.selector == 1 && ev.value == 0) {
                s_state.capture_on = false;
                shown = true;
            }
            ESP_LOGI(TAG, "<- encoder sel %u val %d%s", (unsigned)ev.selector, (int)ev.value, shown ? " (shown from cache)" : "");
            if (shown && lvgl_port_lock(50)) {
                nano_ui_set_state(&s_state, meta);
                lvgl_port_unlock();
            }
            schedule_state(ENCODER_DEBOUNCE_MS);
        } else {
            schedule_state(DEBOUNCE_MS);
        }
        break;
    case NANO_EV_TUNER_PITCH:
        s_last_pitch_us = esp_timer_get_time();
        s_tuner_cleared = false;
        /* Readings still streaming right after our tuner-off are not a pedal-started tuner. */
        if (nano_ui_view() != NANO_VIEW_TUNER && s_last_pitch_us - s_tuner_off_us < TUNER_OFF_GRACE_US) break;
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
                    /* A footswitch-started tuner (the pedal answers our own tuner-on within ~65 ms, and sends
                     * nothing after our tuner-off, capture 2026-09-27), so every on-report is genuine. */
                    nano_ui_open_tuner_from_pedal();
                    s_tuner_view_ours = false;
                }
                s_tuner_muted = ev.tuner_muted;
                nano_ui_set_tuner_mute(ev.tuner_muted);
            } else if (nano_ui_view() == NANO_VIEW_TUNER) {
                /* The pedal ended its tuner (footswitch): close the view without echoing a tuner-off back. */
                s_tuner_view_ours = false;
                nano_ui_close_from_pedal();
            }
            lvgl_port_unlock();
        }
        break;
    case NANO_EV_TAP_TEMPO:
        ESP_LOGI(TAG, "<- tap tempo %s: %.0f BPM", ev.tap_active ? "tap" : "exit", ev.tempo_bpm);
        /* Live tempo while tapping; the first tap opens the tempo view, the pedal's exit closes it.
         * On exit the pedal does not always send a change notice, so re-read. */
        s_state.tempo_bpm = ev.tempo_bpm;
        if (lvgl_port_lock(20)) {
            if (ev.tap_active && nano_ui_view() != NANO_VIEW_TEMPO) {
                nano_ui_open_tempo_from_pedal();
                s_tempo_view_from_pedal = true;
            }
            nano_ui_set_tempo(ev.tempo_bpm, ev.tap_active);
            /* The pedal left the mode itself: close our view without echoing an exit back. */
            if (!ev.tap_active && nano_ui_view() == NANO_VIEW_TEMPO) nano_ui_close_from_pedal();
            lvgl_port_unlock();
        }
        if (!ev.tap_active) {
            s_tempo_view_from_pedal = false;
            schedule_state(DEBOUNCE_MS);
        }
        break;
    case NANO_EV_SETTINGS:
        ESP_LOGI(TAG, "<- settings: outputs 1/2 %s", ev.outputs_muted ? "muted" : "on");
        if (lvgl_port_lock(50)) {
            nano_ui_set_outputs_muted(ev.outputs_muted);
            lvgl_port_unlock();
        }
        break;
    case NANO_EV_RENAME_REPLY:
        ESP_LOGI(TAG, "<- rename preset %u: %s", ev.preset + 1, ev.ok ? "ok" : "refused");
        if (ev.preset == s_rename_idx) rename_answer(ev.ok, "The pedal did not accept this name");
        break;
    case NANO_EV_OUTPUTS_MUTE_ACK:
        ESP_LOGI(TAG, "<- outputs mute ack");
        schedule_settings(SETTINGS_READ_DELAY_MS); /* confirm against the pedal's own report */
        break;
    case NANO_EV_EXPRESSION:
        s_exp_pos = ev.position;
        if (s_exp_show && lvgl_port_lock(20)) {
            nano_ui_set_expression(ev.position);
            lvgl_port_unlock();
        }
        break;
    case NANO_EV_EXP_VALUES:
        /* Paired with every position event; also an empty one after a preset load. */
        if (s_exp_show && lvgl_port_lock(20)) {
            nano_ui_set_expression_values(&ev.exp_values);
            lvgl_port_unlock();
        }
        break;
    case NANO_EV_EXP_ASSIGNMENTS: {
        int preset = s_exp_assign_req;
        s_exp_assign_req = -1;
        if (preset < 0) {
            ESP_LOGW(TAG, "<- expression assignments without a request; ignored");
            break;
        }
        char desc[160];
        int pos = 0;
        static const char *const SLOT[NANO_FX_SLOT_COUNT] = { "pre1", "pre2", "post1", "post2", "post3" };
        for (int i = 0; i < NANO_FX_SLOT_COUNT && pos < (int)sizeof(desc) - 24; i++) {
            if (ev.exp_assign.fx_range[i].assigned) pos += snprintf(desc + pos, sizeof(desc) - pos, " %s %u-%u", SLOT[i], ev.exp_assign.fx_range[i].min, ev.exp_assign.fx_range[i].max);
            if (ev.exp_assign.fx_bypass_mode[i]) pos += snprintf(desc + pos, sizeof(desc) - pos, " %s bypass(m%u)", SLOT[i], ev.exp_assign.fx_bypass_mode[i]);
        }
        ESP_LOGI(TAG, "<- expression assignments of preset %d (%d ms):%s%s%s%s", preset + 1, (int)((esp_timer_get_time() - s_exp_assign_req_us) / 1000), pos ? desc : " none",
                 ev.exp_assign.capture_bypass ? " +capture bypass" : "", ev.exp_assign.ir_bypass ? " +IR bypass" : "",
                 ev.exp_assign.amp_ranges ? " +amp knobs" : "");
        bool same = preset == s_exp_assign_preset;
        s_exp_assign = ev.exp_assign;
        s_exp_assign_preset = preset;
        if (!same && lvgl_port_lock(50)) {
            nano_ui_set_expression_values(NULL); /* the old preset's values do not belong to these targets */
            lvgl_port_unlock();
        }
        exp_push_assignments(s_state.active_preset);
        break;
    }
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
        ESP_LOGI(TAG, "link ready, free heap %u B (lowest %u B)", (unsigned)esp_get_free_heap_size(), (unsigned)esp_get_minimum_free_heap_size());
        s_meta_requested_this_link = false;
        s_req_head = s_req_count = 0;
        s_select_inflight = false;
        s_pending_preset = -1;
        s_settings_read_this_link = false;
        s_settings_due_us = 0;
        nano_assembler_reset(&s_asm);
        request_state();
    } else if (!ready && s_link_ready) {
        s_link_ready = false;
        s_state_due_us = 0;
        s_settings_due_us = 0;
        s_exp_assign_preset = s_exp_assign_req = -1;
        s_exp_assign_tries = 0;
        nano_assembler_reset(&s_asm);
        exp_reset();
        if (lvgl_port_lock(50)) {
            nano_ui_set_outputs_muted(false); /* unknown until the next settings read */
            nano_ui_set_expression_assignments(NULL);
            lvgl_port_unlock();
        }
    }
    cyd_led(!ready && status != NANO_BLE_CONNECTING, false, status == NANO_BLE_CONNECTING);
    if (lvgl_port_lock(100)) {
        char text[48];
        if (ready) {
            snprintf(text, sizeof(text), "Connected");
        } else {
            strncpy(text, detail, sizeof(text) - 1);
            text[sizeof(text) - 1] = '\0';
        }
        nano_ui_set_status(text, ready);
        nano_ui_set_stale(!ready);
        /* Any drop, deliberate or not, shows the connect page: the main view would claim a state we no longer know. */
        if (!ready) nano_ui_set_connected(false);
        lvgl_port_unlock();
    }
}

static void app_task(void *arg)
{
    (void)arg;
    app_msg_t m;
    for (;;) {
        if (xQueueReceive(s_queue, &m, pdMS_TO_TICKS(20)) == pdTRUE) {
            switch (m.kind) {
            case MSG_PACKET:
                if (!nano_is_tuner_pitch_packet(m.pkt.data, m.pkt.len)) ESP_LOGD(TAG, "<- %u B", m.pkt.len);
                if (!s_update_mode) nano_assembler_push(&s_asm, m.pkt.data, m.pkt.len, now_ms());
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
            case MSG_SELECT:
                select_preset_index(m.value);
                break;
            case MSG_CAPTURE_VOLUME:
                set_capture_volume(m.value);
                break;
            case MSG_RENAME:
                rename_preset(m.value, m.text);
                break;
            case MSG_IR_VIEW:
                s_ir_view = m.flag;
                ESP_LOGI(TAG, "IR tab %s, free heap %u B (lowest %u B)", s_ir_view ? "open" : "closed", (unsigned)esp_get_free_heap_size(), (unsigned)esp_get_minimum_free_heap_size());
                if (s_ir_view) request_ir_settings();
                break;
            case MSG_CAB_SETTING:
                if (m.value < NANO_CAB_PARAMS) set_cab_setting((nano_cab_param_t)m.value, m.number);
                break;
            case MSG_CAB_PHASE:
                set_cab_phase(m.flag);
                break;
            case MSG_CAB_MIC:
                if (m.value < NANO_CAB_POSITIONS) set_cab_mic(m.value, m.text);
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
            case MSG_TEMPO_DELTA:
                step_tempo(m.delta);
                break;
            case MSG_TEMPO_VIEW:
                s_tempo_view_from_pedal = false;
                set_tap_mode(m.flag);
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
            case MSG_LABEL_STYLE:
                nvs_save_u8(NVS_KEY_LABEL_STYLE, m.value);
                if (lvgl_port_lock(50)) {
                    nano_ui_set_preset(s_state_valid ? s_state.active_preset : 0, s_meta_valid ? &s_meta_blob.meta : NULL);
                    lvgl_port_unlock();
                }
                break;
            case MSG_OUTPUTS_MUTE:
                set_outputs_mute(m.flag);
                break;
            case MSG_EXP_SHOW:
                s_exp_show = m.flag;
                nvs_save_u8(NVS_KEY_EXP_SHOW, m.flag);
                if (m.flag && lvgl_port_lock(50)) {
                    nano_ui_set_expression(s_exp_pos); /* catch up with what the pedal sent while hidden */
                    lvgl_port_unlock();
                }
                break;
            case MSG_ROTATE:
                ESP_LOGI(TAG, "display rotation %s", m.flag ? "180" : "0");
                nvs_save_u8(NVS_KEY_ROTATE, m.flag ? 1 : 0);
                if (lvgl_port_lock(100)) {
                    cyd_display_set_rotation(m.flag); /* flips the panel and the touch map, redraws */
                    lvgl_port_unlock();
                }
                break;
            case MSG_BRIGHTNESS:
                cyd_backlight_set_level(m.value); /* instant; the value label already shows it */
                nvs_save_u8(NVS_KEY_BRIGHTNESS, m.value);
                break;
            case MSG_UPDATE_OPEN:
                enter_update_mode();
                break;
            case MSG_UPDATE_CLOSE:
                ESP_LOGI(TAG, "update page closed: restarting");
                esp_restart();
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
        if (!s_update_mode) nano_assembler_tick(&s_asm, now_ms());
        ir_push_ui();
        if (s_rename_idx >= 0 && esp_timer_get_time() - s_rename_sent_us > RENAME_TIMEOUT_US) {
            ESP_LOGW(TAG, "rename: no answer");
            rename_answer(false, "No answer from the pedal");
        }
        tempo_edit_tick();
        if (s_select_inflight && esp_timer_get_time() - s_select_sent_us > SELECT_ACK_TIMEOUT_US) {
            ESP_LOGW(TAG, "preset select ack timed out");
            select_settled();
        }
        if (s_link_ready && s_state_due_us && esp_timer_get_time() >= s_state_due_us) request_state();
        if (s_link_ready && s_settings_due_us && esp_timer_get_time() >= s_settings_due_us) request_settings();
        exp_assign_tick();
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
    /* Display settings before the first frame: the backlight is still off. */
    bool rot180 = rotate_load();
    uint8_t brightness = brightness_load();
    cyd_backlight_set_level(brightness);
    nano_ui_callbacks_t ui_cb = {
        .on_prev_preset = ui_on_prev, .on_next_preset = ui_on_next, .on_select_preset = ui_on_select, .on_capture_volume = ui_on_capture_volume, .on_ir_view = ui_on_ir_view, .on_cab_setting = ui_on_cab_setting, .on_cab_phase = ui_on_cab_phase, .on_cab_mic = ui_on_cab_mic, .on_rename_preset = ui_on_rename, .on_toggle_fx = ui_on_toggle_fx,
        .on_tuner = ui_on_tuner, .on_link = ui_on_link, .on_bank_size = ui_on_bank_size,
        .on_toggle_gate = ui_on_toggle_gate, .on_tuner_mute = ui_on_tuner_mute, .on_tempo_delta = ui_on_tempo_delta, .on_tempo_view = ui_on_tempo_view,
        .on_label_style = ui_on_label_style, .on_outputs_mute = ui_on_outputs_mute, .on_expression_show = ui_on_expression_show,
        .on_rotation = ui_on_rotation, .on_brightness = ui_on_brightness,
        .on_update_open = ui_on_update_open, .on_update_close = ui_on_update_close, .on_wifi_scan = ui_on_wifi_scan,
        .on_wifi_join = ui_on_wifi_join, .on_update_check = ui_on_update_check, .on_update_install = ui_on_update_install,
    };
    if (lvgl_port_lock(0)) {
        cyd_display_set_rotation(rot180); /* under the lock: the LVGL task already owns the panel bus */
        nano_ui_create(disp, &ui_cb);
        nano_ui_set_bank_size(bank_load());
        nano_ui_set_label_style(label_style_load());
        s_exp_show = exp_show_load();
        nano_ui_set_expression_show(s_exp_show);
        nano_ui_set_rotation(rot180);
        nano_ui_set_brightness(brightness);
        nano_ui_set_firmware_version(nano_ota_running_version());
        char ssid[33];
        nano_ui_update_set_wifi(nano_ota_saved_ssid(ssid, sizeof(ssid)) ? ssid : NULL);
        nano_ui_set_status("Starting Bluetooth", false);
        nano_ui_set_stale(true);
        nano_ui_set_connected(false);
        lvgl_port_unlock();
    }
    vTaskDelay(pdMS_TO_TICKS(60)); /* let the first frame flush before lighting the panel */
    cyd_backlight(true);

    s_queue = xQueueCreate(PACKET_QUEUE_LEN, sizeof(app_msg_t));
    s_asm_buf = malloc(ASSEMBLER_CAP);
    s_meta_scratch = malloc(sizeof(*s_meta_scratch));
    if (!s_queue || !s_asm_buf || !s_meta_scratch || nano_ota_init() != 0) {
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
    /* Display, touch and Bluetooth came up: a freshly installed image is good (else the bootloader rolls back). */
    nano_ota_mark_valid();
    ESP_LOGI(TAG, "firmware %s, free heap %u B", nano_ota_running_version(), (unsigned)esp_get_free_heap_size());
}
