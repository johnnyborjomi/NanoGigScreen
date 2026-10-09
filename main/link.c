#include "link.h"

#include <string.h>

#include "app.h"
#include "cyd_board.h"
#include "esp_log.h"
#include "esp_system.h"
#include "expression.h"
#include "nano_ui.h"
#include "pedal_in.h"
#include "preset_select.h"
#include "remote_page.h"
#include "rename.h"
#include "settings.h"
#include "tempo.h"
#include "tuner.h"
#include "update_mode.h"

static const char *TAG = "link";

#define STATE_MIN_GAP_US (200 * 1000)  /* a select's ack events also ask; one dump is enough */
#define SETTINGS_READ_DELAY_MS 400     /* after the first state dump / a mute ack: one write at a time on the link */
#define REQ_FIFO_LEN 8

static struct {
    char status_text[48];
    bool meta_requested;        /* this link */
    int64_t state_due_us;       /* 0 = no pending re-read */
    int64_t state_sent_us;      /* when the last state request went out */
    /* Every state request's send time, oldest first: a dump answers the oldest one, so we know how old it is. */
    int64_t req_fifo[REQ_FIFO_LEN];
    int req_head, req_count;
    /* Device settings (outputs 1/2 mute) are read once per link after the first state dump, and after every mute ack. */
    bool settings_read;         /* this link */
    int64_t settings_due_us;    /* 0 = none pending */
    bool outputs_muted;         /* as shown */
} s = { .status_text = "Starting Bluetooth" };

/* ---- state ----------------------------------------------------------------- */

void link_request_state(void)
{
    s.state_due_us = 0;
    int64_t now = app_now_us();
    if (now - s.state_sent_us < STATE_MIN_GAP_US) {
        s.state_due_us = s.state_sent_us + STATE_MIN_GAP_US; /* not dropped: sent at the end of the gap */
        return;
    }
    s.state_sent_us = now;
    if (app_send(NANO_REQ_STATE, sizeof(NANO_REQ_STATE))) {
        ESP_LOGI(TAG, "-> state request");
        if (s.req_count < REQ_FIFO_LEN) s.req_fifo[(s.req_head + s.req_count++) % REQ_FIFO_LEN] = now;
    } else {
        s.state_due_us = now + STATE_MIN_GAP_US; /* the write failed: retried, not lost */
    }
}

void link_schedule_state(uint32_t delay_ms)
{
    int64_t due = app_now_us() + (int64_t)delay_ms * 1000;
    if (s.state_due_us == 0 || due < s.state_due_us) s.state_due_us = due;
}

int64_t link_pop_request_time(void)
{
    if (s.req_count == 0) return s.state_sent_us;
    int64_t t = s.req_fifo[s.req_head];
    s.req_head = (s.req_head + 1) % REQ_FIFO_LEN;
    s.req_count--;
    return t;
}

/* ---- metadata ---------------------------------------------------------------- */

/* A failed write leaves the flag clear: the next state dump asks again. */
static void request_metadata(void)
{
    if (!app_send(NANO_REQ_METADATA, sizeof(NANO_REQ_METADATA))) return;
    s.meta_requested = true;
    ESP_LOGI(TAG, "-> metadata request (~6 s)");
}

bool link_metadata_requested(void) { return s.meta_requested; }

/* Compare the dump's own preset against its cache record (never a pending target against another preset's names). */
static bool cache_contradicts_state(const nano_state_t *in)
{
    const nano_metadata_t *meta = app_meta();
    if (!meta) return true;
    const nano_preset_record_t *p = &meta->presets[in->active_preset];
    if (!p->name[0] && !p->capture_name[0]) return in->capture_name[0] != 0; /* empty slot on both sides is fine */
    return strcmp(p->capture_name, in->capture_name) != 0;
}

/* ---- device settings ----------------------------------------------------------- */

static void schedule_settings(uint32_t delay_ms)
{
    s.settings_due_us = app_now_us() + (int64_t)delay_ms * 1000;
}

static void request_settings(void)
{
    s.settings_due_us = 0;
    if (!g_app.link_ready) return;
    if (!app_send(NANO_REQ_SETTINGS, sizeof(NANO_REQ_SETTINGS))) {
        schedule_settings(SETTINGS_READ_DELAY_MS); /* retried */
        return;
    }
    s.settings_read = true;
    ESP_LOGI(TAG, "-> settings request");
}

void link_on_state(const nano_state_t *dump, bool switching)
{
    if (!s.meta_requested && !switching && cache_contradicts_state(dump)) request_metadata();
    else if (!s.settings_read && !s.settings_due_us) schedule_settings(SETTINGS_READ_DELAY_MS);
}

void link_on_metadata(void)
{
    /* The state inside the metadata reply is as old as the request: ask for a fresh one. */
    link_request_state();
    if (!s.settings_read) schedule_settings(SETTINGS_READ_DELAY_MS);
}

void link_on_settings(bool outputs_muted)
{
    ESP_LOGI(TAG, "<- settings: outputs 1/2 %s", outputs_muted ? "muted" : "on");
    s.outputs_muted = outputs_muted;
    ui_mark(UI_OUTPUTS);
}

void link_on_outputs_mute_ack(void)
{
    ESP_LOGI(TAG, "<- outputs mute ack");
    schedule_settings(SETTINGS_READ_DELAY_MS); /* confirm against the pedal's own report */
}

/* Outputs 1/2 mute (Cortex Cloud's global switch); the 0x44 ack and the settings reply's field 16 confirm. */
void link_set_outputs_mute(bool mute)
{
    if (!g_app.link_ready) return;
    uint8_t f[NANO_FRAME_MAX];
    if (app_send(f, nano_build_outputs_mute(f, sizeof(f), mute))) ESP_LOGI(TAG, "-> outputs 1/2 %s", mute ? "mute" : "on");
    schedule_settings(SETTINGS_READ_DELAY_MS); /* also puts the badge back if the write failed */
}

/* ---- link up / down --------------------------------------------------------------- */

/* In-flight work only means something on its own link: dropped on both edges, so nothing waits for an answer
 * that can no longer come and a new link starts clean. */
static void drop_link_work(void)
{
    s.state_due_us = 0;
    s.req_head = s.req_count = 0;
    s.meta_requested = false;
    s.settings_read = false;
    s.settings_due_us = 0;
    pedal_in_link_reset();
    preset_select_link_reset();
    tempo_link_reset();
    tuner_link_reset();
    expression_link_reset();
    remote_pages_link_reset();
    rename_link_reset();
}

void link_on_status(nano_ble_status_t status, const char *detail)
{
    bool ready = status == NANO_BLE_READY;
    if (ready && !g_app.link_ready) {
        g_app.link_ready = true;
        ESP_LOGI(TAG, "link ready, free heap %u B (lowest %u B)", (unsigned)esp_get_free_heap_size(), (unsigned)esp_get_minimum_free_heap_size());
        update_mode_keep_image();
        drop_link_work();
        link_request_state();
    } else if (!ready && g_app.link_ready) {
        g_app.link_ready = false;
        ui_unmark(UI_PARTS_OF_LINK); /* waiting for the display: must not refill the greyed-out screen */
        drop_link_work();           /* (after: its resets mark what they clear) */
        s.outputs_muted = false;    /* unknown until the next settings read */
        ui_mark(UI_OUTPUTS);
    }
    cyd_led(!ready && status != NANO_BLE_CONNECTING, false, status == NANO_BLE_CONNECTING);
    if (!ready) strlcpy(s.status_text, detail ? detail : "", sizeof(s.status_text));
    ui_mark(UI_LINK);
}

void link_tick(int64_t now)
{
    if (!g_app.link_ready) return;
    if (s.state_due_us && now >= s.state_due_us) link_request_state();
    if (s.settings_due_us && now >= s.settings_due_us) request_settings();
}

void link_ui_push(uint32_t parts)
{
    if (parts & UI_OUTPUTS) nano_ui_set_outputs_muted(s.outputs_muted);
    if (parts & UI_LINK) {
        nano_ui_set_status(g_app.link_ready ? "Connected" : s.status_text, g_app.link_ready);
        nano_ui_set_stale(!g_app.link_ready);
        /* Any drop, deliberate or not, shows the connect page: the main view would claim a state we no longer know. */
        if (!g_app.link_ready) nano_ui_set_connected(false);
    }
}
