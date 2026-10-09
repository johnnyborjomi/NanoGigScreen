/*
 * NanoGig Screen: a standalone Bluetooth gig view for the Neural DSP Nano Cortex on an ESP32-2432S028 (Cheap Yellow
 * Display).
 *
 * Tasks: the NimBLE host task (nano_ble) and the LVGL task (nano_ui) only post to the app task, which owns all the
 * app's state and is the only one that writes to the pedal; the update task (nano_ota) drives the update page. The
 * app task's modules (app.h) each own one concern:
 *   link.c          session: link up / down, state / metadata / settings reads, outputs mute
 *   pedal_in.c      reassembly, decoding and routing of what the pedal sends
 *   preset_select.c block_edits.c tempo.c tuner.c expression.c rename.c: one feature each
 *   remote_page.c   settings read while their page shows (ir_page.c)
 *   settings.c      the screen's own settings and the name cache in flash
 *   update_mode.c   Wi-Fi firmware update
 *   app.c           shared state, sending frames, screen sync
 */
#include <string.h>

#include "app.h"
#include "block_edits.h"
#include "cyd_board.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_system.h"
#include "expression.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "ir_library.h"
#include "ir_page.h"
#include "link.h"
#include "nano_ble.h"
#include "nano_ota.h"
#include "nano_ui.h"
#include "nvs_flash.h"
#include "pedal_in.h"
#include "preset_select.h"
#include "rename.h"
#include "settings.h"
#include "tempo.h"
#include "tuner.h"
#include "update_mode.h"

static const char *TAG = "nanogig";

#define PACKET_CAP 514              /* MTU 517 - 3 */
#define PACKET_QUEUE_LEN 12
#define CMD_QUEUE_LEN 24            /* screen / link commands: their own queue, so a dump in flight never crowds them out */
#define LOOP_MS 20
#define APP_TASK_STACK 8192
#define APP_TASK_PRIO 5
#define APP_TASK_CORE 1

typedef struct {
    uint16_t len;
    uint8_t data[PACKET_CAP];
} packet_t;

typedef enum {
    MSG_STATUS, MSG_PREV, MSG_NEXT, MSG_SELECT, MSG_TOGGLE_FX, MSG_TOGGLE_GATE, MSG_CAPTURE_VOLUME, MSG_RENAME,
    MSG_IR_VIEW, MSG_CAB_SETTING, MSG_CAB_PHASE, MSG_CAB_ON, MSG_CAB_STEP, MSG_IR_LIBRARY, MSG_IR_PICK, MSG_CAB_MIC, MSG_TUNER, MSG_TUNER_MUTE, MSG_TEMPO_DELTA, MSG_TEMPO_VIEW,
    MSG_LINK, MSG_BANK_SIZE, MSG_LABEL_STYLE, MSG_OUTPUTS_MUTE, MSG_EXP_SHOW, MSG_ROTATE, MSG_BRIGHTNESS,
    MSG_UPDATE_OPEN, MSG_UPDATE_CLOSE,
} msg_kind_t;

#define MSG_TEXT_CAP 48 /* an IR name from the library */
_Static_assert(NANO_PRESET_NAME_MAX + 1 <= MSG_TEXT_CAP, "a preset name fits a message");
typedef struct {
    msg_kind_t kind;
    nano_ble_status_t status; /* MSG_STATUS */
    bool flag;      /* on / open / connect / mute / show / 180 degrees / phase inverted / the tile's current state */
    int delta;      /* MSG_TEMPO_DELTA, MSG_CAB_STEP */
    uint8_t value;  /* preset index, FX slot, raw capture volume, IR parameter / mic position / list, a setting's value */
    uint16_t index; /* MSG_IR_PICK: the IR's position in its list */
    float number;   /* MSG_CAB_SETTING: the pedal's 0..1 */
    char text[MSG_TEXT_CAP]; /* MSG_STATUS: the detail, MSG_RENAME: the new name, MSG_CAB_MIC: the microphone, MSG_IR_PICK: the IR */
} app_msg_t;

static QueueHandle_t s_packets, s_cmds; /* both feed the app task through s_inbox, in arrival order */
static QueueSetHandle_t s_inbox;

/* ---- BLE / screen callbacks: copy and post to the app task ----------------- */

/* Commands come from the LVGL task (which must not block) and the NimBLE host task (status). A full queue means
 * the app task is stuck: logged, never silent. */
static void post(const app_msg_t *m, TickType_t wait)
{
    if (xQueueSend(s_cmds, m, wait) != pdTRUE) ESP_LOGW(TAG, "command queue full, dropped kind %d", (int)m->kind);
}

static void post_kind(msg_kind_t kind) { app_msg_t m = { .kind = kind }; post(&m, 0); }
static void post_flag(msg_kind_t kind, bool flag) { app_msg_t m = { .kind = kind, .flag = flag }; post(&m, 0); }
static void post_value(msg_kind_t kind, uint8_t value) { app_msg_t m = { .kind = kind, .value = value }; post(&m, 0); }
static void post_text(msg_kind_t kind, uint8_t value, const char *text)
{
    app_msg_t m = { .kind = kind, .value = value };
    strlcpy(m.text, text ? text : "", sizeof(m.text));
    post(&m, 0);
}

static void ble_on_status(nano_ble_status_t status, const char *detail)
{
    app_msg_t m = { .kind = MSG_STATUS, .status = status };
    strlcpy(m.text, detail ? detail : "", sizeof(m.text));
    post(&m, pdMS_TO_TICKS(50));
}

static void ble_on_notify(const uint8_t *data, size_t len)
{
    if (len > PACKET_CAP) len = PACKET_CAP;
    static packet_t pkt; /* host task only; the queue copies by value */
    pkt.len = (uint16_t)len;
    memcpy(pkt.data, data, len);
    if (xQueueSend(s_packets, &pkt, pdMS_TO_TICKS(50)) != pdTRUE) ESP_LOGW(TAG, "packet queue full, dropped %u B", (unsigned)len);
}

static void ui_on_prev(void) { post_kind(MSG_PREV); }
static void ui_on_next(void) { post_kind(MSG_NEXT); }
static void ui_on_select(uint8_t idx) { post_value(MSG_SELECT, idx); }
static void ui_on_rename(uint8_t idx, const char *name) { post_text(MSG_RENAME, idx, name); }
static void ui_on_capture_volume(uint8_t raw) { post_value(MSG_CAPTURE_VOLUME, raw); }
static void ui_on_ir_view(bool open) { post_flag(MSG_IR_VIEW, open); }
static void ui_on_cab_setting(uint8_t param, float n) { app_msg_t m = { .kind = MSG_CAB_SETTING, .value = param, .number = n }; post(&m, 0); }
static void ui_on_cab_phase(bool inverted) { post_flag(MSG_CAB_PHASE, inverted); }
static void ui_on_cab_on(bool on) { post_flag(MSG_CAB_ON, on); }
static void ui_on_ir_library(bool open) { post_flag(MSG_IR_LIBRARY, open); }
static void ui_on_ir_pick(uint8_t list, uint16_t index, const char *name) { app_msg_t m = { .kind = MSG_IR_PICK, .value = list, .index = index }; strlcpy(m.text, name, sizeof(m.text)); post(&m, 0); }
static void ui_on_cab_step(int delta) { app_msg_t m = { .kind = MSG_CAB_STEP, .delta = delta }; post(&m, 0); }
static void ui_on_cab_mic(uint8_t position, const char *mic) { post_text(MSG_CAB_MIC, position, mic); }
static void ui_on_toggle_fx(uint8_t slot, bool on) { app_msg_t m = { .kind = MSG_TOGGLE_FX, .value = slot, .flag = on }; post(&m, 0); }
static void ui_on_toggle_gate(bool on) { post_flag(MSG_TOGGLE_GATE, on); }
static void ui_on_tempo_view(bool open) { post_flag(MSG_TEMPO_VIEW, open); }
static void ui_on_tempo_delta(int d) { app_msg_t m = { .kind = MSG_TEMPO_DELTA, .delta = d }; post(&m, 0); }
static void ui_on_tuner_mute(bool mute) { post_flag(MSG_TUNER_MUTE, mute); }
static void ui_on_tuner(bool on) { post_flag(MSG_TUNER, on); }
static void ui_on_link(bool connect) { post_flag(MSG_LINK, connect); }
static void ui_on_bank_size(uint8_t v) { post_value(MSG_BANK_SIZE, v); }
static void ui_on_label_style(uint8_t v) { post_value(MSG_LABEL_STYLE, v); }
static void ui_on_outputs_mute(bool mute) { post_flag(MSG_OUTPUTS_MUTE, mute); }
static void ui_on_expression_show(bool show) { post_flag(MSG_EXP_SHOW, show); }
static void ui_on_rotation(bool rot) { post_flag(MSG_ROTATE, rot); }
static void ui_on_brightness(uint8_t v) { post_value(MSG_BRIGHTNESS, v); }
/* Update mode: opening shuts Bluetooth down (blocking, so on the app task); the rest goes straight to the update task. */
static void ui_on_update_open(void) { post_kind(MSG_UPDATE_OPEN); }
static void ui_on_update_close(void) { post_kind(MSG_UPDATE_CLOSE); }
static void ui_on_wifi_scan(void) { nano_ota_scan(); }
static void ui_on_wifi_join(const char *ssid, const char *pass) { nano_ota_join(ssid, pass); }
static void ui_on_update_check(void) { nano_ota_check(); }
static void ui_on_update_install(void) { nano_ota_install(); }

/* ---- app task ----------------------------------------------------------------- */

static void on_command(const app_msg_t *m)
{
    switch (m->kind) {
    case MSG_STATUS: link_on_status(m->status, m->text); break;
    case MSG_PREV: preset_select_step(-1); break;
    case MSG_NEXT: preset_select_step(+1); break;
    case MSG_SELECT: preset_select_index(m->value); break;
    case MSG_TOGGLE_FX: block_edits_toggle_fx(m->value, m->flag); break;
    case MSG_TOGGLE_GATE: block_edits_toggle_gate(m->flag); break;
    case MSG_CAPTURE_VOLUME: block_edits_set_capture_volume(m->value); break;
    case MSG_RENAME: rename_preset(m->value, m->text); break;
    case MSG_IR_VIEW: ir_page_set_open(m->flag); break;
    case MSG_CAB_SETTING: ir_page_set_param((nano_cab_param_t)m->value, m->number); break;
    case MSG_CAB_PHASE: ir_page_set_phase(m->flag); break;
    case MSG_CAB_ON: block_edits_set_ir_on(m->flag); break;
    case MSG_CAB_STEP: block_edits_step_ir(m->delta); break;
    case MSG_IR_LIBRARY: ir_library_set_open(m->flag); break;
    case MSG_IR_PICK: ir_library_pick(m->value, m->index, m->text); break;
    case MSG_CAB_MIC: ir_page_set_mic(m->value, m->text); break;
    case MSG_TUNER: tuner_set(m->flag); break;
    case MSG_TUNER_MUTE: tuner_set_mute(m->flag); break;
    case MSG_TEMPO_DELTA: tempo_step(m->delta); break;
    case MSG_TEMPO_VIEW: tempo_view(m->flag); break;
    case MSG_OUTPUTS_MUTE: link_set_outputs_mute(m->flag); break;
    case MSG_LINK:
        ESP_LOGI(TAG, "link %s", m->flag ? "enabled" : "disabled");
        nano_ble_set_enabled(m->flag);
        break;
    case MSG_BANK_SIZE:
        settings_set_bank_size(m->value);
        ui_mark(UI_PRESET);
        break;
    case MSG_LABEL_STYLE:
        settings_set_label_style(m->value);
        ui_mark(UI_PRESET);
        break;
    case MSG_EXP_SHOW: expression_set_show(m->flag); break;
    case MSG_ROTATE: settings_set_rotation(m->flag); break;
    case MSG_BRIGHTNESS: settings_set_brightness(m->value); break;
    case MSG_UPDATE_OPEN: update_mode_enter(); break;
    case MSG_UPDATE_CLOSE:
        settings_flush();
        ESP_LOGI(TAG, "update page closed: restarting");
        esp_restart();
        break;
    }
}

/* Deadlines, polled every loop (one task, no timers to race with). */
static void app_tick(void)
{
    int64_t now = app_now_us();
    pedal_in_tick();
    ui_sync(); /* parts the display was too busy for */
    rename_tick(now);
    tempo_tick(now);
    preset_select_tick(now);
    link_tick(now);
    expression_tick(now);
    tuner_tick(now);
    settings_tick(now);
    update_mode_tick(now);
}

static void app_task(void *arg)
{
    (void)arg;
    for (;;) {
        /* The set hands out its queues in arrival order: a status change and the packets after it stay in order. */
        QueueSetMemberHandle_t q = xQueueSelectFromSet(s_inbox, pdMS_TO_TICKS(LOOP_MS));
        if (q == s_cmds) {
            app_msg_t m;
            if (xQueueReceive(s_cmds, &m, 0) == pdTRUE) on_command(&m);
        } else if (q == s_packets) {
            static packet_t pkt; /* app task only */
            if (xQueueReceive(s_packets, &pkt, 0) == pdTRUE && !update_mode_active()) pedal_in_packet(pkt.data, pkt.len);
        }
        app_tick();
    }
}

/* ---- start ----------------------------------------------------------------------- */

/* Shown on the status line when start-up cannot go on. */
static void fail_at_start(const char *what)
{
    ESP_LOGE(TAG, "%s", what);
    if (lvgl_port_lock(0)) {
        nano_ui_set_status(what, false);
        lvgl_port_unlock();
    }
}

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }
    settings_load();
    ir_page_init();

    /* Queues before the screen: its callbacks post to them from the first touch on. */
    s_packets = xQueueCreate(PACKET_QUEUE_LEN, sizeof(packet_t));
    s_cmds = xQueueCreate(CMD_QUEUE_LEN, sizeof(app_msg_t));
    s_inbox = xQueueCreateSet(PACKET_QUEUE_LEN + CMD_QUEUE_LEN);
    bool ready = s_packets && s_cmds && s_inbox && pedal_in_init();
    if (ready) {
        xQueueAddToSet(s_packets, s_inbox);
        xQueueAddToSet(s_cmds, s_inbox);
    }

    lv_display_t *disp = cyd_board_init();
    if (!disp) {
        ESP_LOGE(TAG, "display init failed");
        return;
    }
    /* Display settings before the first frame: the backlight is still off. */
    cyd_backlight_set_level(settings_brightness());
    nano_ui_callbacks_t ui_cb = {
        .on_prev_preset = ui_on_prev, .on_next_preset = ui_on_next, .on_select_preset = ui_on_select, .on_rename_preset = ui_on_rename,
        .on_capture_volume = ui_on_capture_volume, .on_ir_view = ui_on_ir_view, .on_cab_setting = ui_on_cab_setting,
        .on_cab_phase = ui_on_cab_phase, .on_cab_on = ui_on_cab_on, .on_cab_step = ui_on_cab_step, .on_ir_library = ui_on_ir_library, .on_ir_pick = ui_on_ir_pick, .on_cab_mic = ui_on_cab_mic, .on_toggle_fx = ui_on_toggle_fx, .on_toggle_gate = ui_on_toggle_gate,
        .on_tuner = ui_on_tuner, .on_tuner_mute = ui_on_tuner_mute, .on_tempo_delta = ui_on_tempo_delta, .on_tempo_view = ui_on_tempo_view,
        .on_link = ui_on_link, .on_bank_size = ui_on_bank_size, .on_label_style = ui_on_label_style, .on_outputs_mute = ui_on_outputs_mute,
        .on_expression_show = ui_on_expression_show, .on_rotation = ui_on_rotation, .on_brightness = ui_on_brightness,
        .on_update_open = ui_on_update_open, .on_update_close = ui_on_update_close, .on_wifi_scan = ui_on_wifi_scan,
        .on_wifi_join = ui_on_wifi_join, .on_update_check = ui_on_update_check, .on_update_install = ui_on_update_install,
    };
    if (lvgl_port_lock(0)) {
        cyd_display_set_rotation(settings_rotate_180()); /* under the lock: the LVGL task already owns the panel bus */
        nano_ui_create(disp, &ui_cb);
        nano_ui_set_bank_size(settings_bank_size());
        nano_ui_set_label_style(settings_label_style());
        nano_ui_set_expression_show(settings_exp_show());
        nano_ui_set_rotation(settings_rotate_180());
        nano_ui_set_brightness(settings_brightness());
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

    if (!ready || nano_ota_init() != 0) {
        fail_at_start("Out of memory at start");
        return;
    }
    if (xTaskCreatePinnedToCore(app_task, "nanogig_app", APP_TASK_STACK, NULL, APP_TASK_PRIO, NULL, APP_TASK_CORE) != pdPASS) {
        fail_at_start("App task failed to start");
        return;
    }
    nano_ble_callbacks_t ble_cb = { .on_status = ble_on_status, .on_notify = ble_on_notify };
    if (nano_ble_start(&ble_cb) != 0) fail_at_start("Bluetooth failed to start");
    /* The image is kept once it links to the pedal or has run a minute (update_mode.c). */
    ESP_LOGI(TAG, "firmware %s, free heap %u B", nano_ota_running_version(), (unsigned)esp_get_free_heap_size());
}
