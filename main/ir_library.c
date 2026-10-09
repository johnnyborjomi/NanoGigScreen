#include "ir_library.h"

#include <stdlib.h>
#include <string.h>

#include "app.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nano_ui.h"

static const char *TAG = "irlib";

static struct {
    bool open;
    bool asked;               /* a request is out (its reply is ours) */
    bool irs_only;            /* the request in flight left the captures out */
    nano_ir_library_t *lib;   /* decoded, not yet on screen (the screen frees it) */
} s;

static void request(bool irs_only)
{
    uint8_t f[NANO_FRAME_MAX];
    if (!g_app.link_ready || !app_send(f, nano_build_library_request(f, sizeof(f), irs_only))) return;
    s.asked = true;
    s.irs_only = irs_only;
    ESP_LOGI(TAG, "-> library request (%s)", irs_only ? "IRs" : "captures and IRs");
}

void ir_library_set_open(bool open)
{
    s.open = open;
    ESP_LOGI(TAG, "IR list %s, free heap %u B (lowest %u B)", open ? "open" : "closed", (unsigned)esp_get_free_heap_size(), (unsigned)esp_get_minimum_free_heap_size());
    if (open) {
        ui_mark(UI_IR_FOLDERS);
        request(true);
        return;
    }
    s.asked = false; /* a late reply is dropped */
    free(s.lib);
    s.lib = NULL;
}

bool ir_library_on_reply(int msg_type, const uint8_t *payload, size_t len)
{
    if (msg_type != NANO_MSG_LIBRARY) return false;
    if (!s.open || !s.asked) return true;
    s.asked = false;
    size_t need = nano_decode_ir_library(payload, len, NULL, 0);
    if (!need) {
        ESP_LOGW(TAG, "<- library of %u B without IRs%s", (unsigned)len, s.irs_only ? ": asking for everything" : "");
        if (s.irs_only) request(false);
        return true;
    }
    nano_ir_library_t *lib = malloc(need);
    if (!lib) {
        ESP_LOGE(TAG, "no memory for the IR library (%u B)", (unsigned)need);
        return true;
    }
    nano_decode_ir_library(payload, len, lib, need);
    ESP_LOGI(TAG, "<- library %u B: %u factory IRs, %u user IRs (%u B kept)", (unsigned)len, lib->count[NANO_IR_FACTORY],
             lib->count[NANO_IR_USER], (unsigned)need);
    free(s.lib);
    s.lib = lib;
    ui_mark(UI_IR_LIBRARY);
    return true;
}

/* A live edit like the IR loader's in Cortex Cloud: the name shows at once, the next dump confirms it. */
void ir_library_pick(uint8_t list, uint16_t index, const char *name)
{
    if (!g_app.link_ready || !g_app.state_valid) return;
    uint8_t f[NANO_FRAME_MAX];
    if (!app_send(f, nano_build_cab_load(f, sizeof(f), list, index, name))) return;
    ESP_LOGI(TAG, "-> load %s IR %u \"%s\"", list == NANO_IR_USER ? "user" : "factory", index + 1, name);
    g_app.state.cab_on = true;
    strlcpy(g_app.state.ir_short_name, name, sizeof(g_app.state.ir_short_name));
    ui_mark(UI_STATE);
    link_schedule_state(CONFIRM_MS);
}

void ir_library_link_reset(void)
{
    s.asked = false;
    free(s.lib);
    s.lib = NULL;
    ui_unmark(UI_IR_LIBRARY);
}

void ir_library_ui_push(uint32_t parts)
{
    if (!(parts & UI_IR_LIBRARY) || !s.lib) return;
    nano_ui_set_ir_library(s.lib); /* the screen owns it now */
    s.lib = NULL;
}
