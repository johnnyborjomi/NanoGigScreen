#include "rename.h"

#include <stdio.h>
#include <string.h>

#include "app.h"
#include "esp_log.h"
#include "nano_ui.h"
#include "settings.h"

static const char *TAG = "rename";

#define TIMEOUT_US (3000 * 1000)

static struct {
    int index;                              /* waiting for the answer for this preset; -1 = none */
    char name[NANO_PRESET_NAME_MAX + 1];
    int64_t sent_us;
    struct { int index; bool ok; char msg[48]; } result; /* for the screen (UI_RENAME) */
} s = { .index = -1 };

static void answer(bool ok, const char *msg)
{
    int index = s.index;
    s.index = -1;
    if (index < 0) return;
    if (ok && g_app.meta_valid) {
        snprintf(g_app.meta->presets[index].name, sizeof(g_app.meta->presets[index].name), "%s", s.name);
        settings_save_meta();
    }
    s.result.index = index;
    s.result.ok = ok;
    strlcpy(s.result.msg, msg ? msg : "", sizeof(s.result.msg));
    ui_mark(UI_RENAME | (ok ? UI_PRESET : 0));
}

void rename_preset(uint8_t index, const char *name)
{
    s.index = index;
    uint8_t f[NANO_FRAME_MAX];
    if (!g_app.link_ready || !app_send(f, nano_build_preset_rename(f, sizeof(f), index, name))) {
        answer(false, "Not connected to the pedal");
        return;
    }
    ESP_LOGI(TAG, "-> rename preset %u to \"%s\"", index + 1, name);
    strlcpy(s.name, name, sizeof(s.name));
    s.sent_us = app_now_us();
}

void rename_on_reply(const nano_event_t *ev)
{
    ESP_LOGI(TAG, "<- rename preset %u: %s", ev->preset + 1, ev->ok ? "ok" : "refused");
    if (ev->preset == s.index) answer(ev->ok, "The pedal did not accept this name");
}

void rename_tick(int64_t now)
{
    if (s.index >= 0 && now - s.sent_us > TIMEOUT_US) {
        ESP_LOGW(TAG, "no answer");
        answer(false, "No answer from the pedal");
    }
}

void rename_link_reset(void)
{
    if (s.index >= 0) answer(false, "Connection to the pedal lost");
}

void rename_ui_push(uint32_t parts)
{
    if (parts & UI_RENAME) nano_ui_rename_result((uint8_t)s.result.index, s.result.ok, s.result.msg);
}
