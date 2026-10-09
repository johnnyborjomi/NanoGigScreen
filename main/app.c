#include "app.h"

#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_timer.h"
#include "expression.h"
#include "ir_folder_store.h"
#include "ir_library.h"
#include "link.h"
#include "nano_ble.h"
#include "nano_ui.h"
#include "remote_page.h"
#include "rename.h"
#include "settings.h"
#include "tuner.h"

static const char *TAG = "app";

app_t g_app;

int64_t app_now_us(void) { return esp_timer_get_time(); }

bool app_send(const uint8_t *frame, size_t n)
{
    if (!n) {
        ESP_LOGW(TAG, "frame did not build (bad value or too long); not sent");
        return false;
    }
    return nano_ble_write(frame, n) == 0;
}

/* ---- screen sync ------------------------------------------------------------- */

static uint32_t s_dirty;

/* The parts g_app itself feeds: the gig view. */
static void state_ui_push(uint32_t d)
{
    const nano_metadata_t *meta = app_meta();
    if ((d & UI_STATE) && g_app.state_valid) nano_ui_set_state(&g_app.state, meta);
    if (d & UI_SYNCED) {
        nano_ui_set_stale(false);
        nano_ui_set_connected(true); /* the connect page closes only once the pedal's state is on screen */
    }
    if (d & UI_FOOTSWITCHES) nano_ui_set_footswitches(g_app.state.footswitch);
    /* The optimistic target during a select; 0 before the first state. */
    if (d & UI_PRESET) nano_ui_set_preset(g_app.state.active_preset, meta);
}

/* In this order; the link's part goes last: a drop greys out and blanks whatever the others showed. */
static void (*const PUSHERS[])(uint32_t parts) = {
    state_ui_push, expression_ui_push, remote_pages_ui_push, ir_library_ui_push, ir_folder_store_ui_push, rename_ui_push, tuner_ui_push, settings_ui_push, link_ui_push,
};

void ui_mark(uint32_t parts)
{
    s_dirty |= parts;
    ui_sync();
}

void ui_unmark(uint32_t parts)
{
    s_dirty &= ~parts;
}

void ui_sync(void)
{
    if (!s_dirty || !lvgl_port_lock(UI_LOCK_MS)) return;
    uint32_t d = s_dirty;
    s_dirty = 0;
    for (size_t i = 0; i < sizeof(PUSHERS) / sizeof(PUSHERS[0]); i++) PUSHERS[i](d);
    lvgl_port_unlock();
}
