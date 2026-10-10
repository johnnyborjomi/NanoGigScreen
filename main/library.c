#include "library.h"

#include <stdlib.h>
#include <string.h>

#include "app.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nano_ui.h"

static const char *TAG = "library";

static struct {
    bool open;
    uint8_t source;           /* the open list's: NANO_SOURCE_CAPTURE / NANO_SOURCE_IR */
    bool asked;               /* a request is out (its reply is ours) */
    int what;                 /* what the request in flight asked for (NANO_LIB_*) */
    nano_library_t *lib;      /* decoded, not yet on screen (the screen frees it) */
} s;

static const char *source_name(uint8_t source) { return source == NANO_SOURCE_IR ? "IR" : "capture"; }
static const char *list_name(uint8_t list) { return list == NANO_IR_USER ? "user" : "factory"; }

static void request(int what)
{
    uint8_t f[NANO_FRAME_MAX];
    if (!g_app.link_ready || !app_send(f, nano_build_library_request(f, sizeof(f), what))) return;
    s.asked = true;
    s.what = what;
    ESP_LOGI(TAG, "-> library request (%s)", what == NANO_LIB_IRS ? "IRs" : what == NANO_LIB_CAPTURES ? "captures" : "captures and IRs");
}

void library_set_open(uint8_t source, bool open)
{
    ESP_LOGI(TAG, "%s list %s, free heap %u B (lowest %u B)", source_name(source), open ? "open" : "closed", (unsigned)esp_get_free_heap_size(),
             (unsigned)esp_get_minimum_free_heap_size());
    if (!open && source != s.source) return; /* another list closed late */
    s.open = open;
    s.source = source;
    free(s.lib);
    s.lib = NULL;
    s.asked = false; /* a late reply is dropped */
    if (open) {
        ui_mark(UI_IR_FOLDERS);
        request(source == NANO_SOURCE_IR ? NANO_LIB_IRS : NANO_LIB_CAPTURES);
    }
}

static void on_library(const uint8_t *payload, size_t len)
{
    if (!s.open || !s.asked) return;
    s.asked = false;
    int what = s.source == NANO_SOURCE_IR ? NANO_LIB_IRS : NANO_LIB_CAPTURES;
    size_t need = nano_decode_library(payload, len, what, NULL, 0);
    if (!need) {
        ESP_LOGW(TAG, "<- library of %u B without %ss%s", (unsigned)len, source_name(s.source), s.what == what ? ": asking for everything" : "");
        if (s.what == what) request(NANO_LIB_CAPTURES | NANO_LIB_IRS);
        return;
    }
    nano_library_t *lib = malloc(need);
    if (!lib) {
        ESP_LOGE(TAG, "no memory for the library (%u B)", (unsigned)need);
        return;
    }
    nano_decode_library(payload, len, what, lib, need);
    ESP_LOGI(TAG, "<- library %u B: %u factory %ss, %u user (%u B kept)", (unsigned)len, lib->count[NANO_IR_FACTORY], source_name(s.source),
             lib->count[NANO_IR_USER], (unsigned)need);
    free(s.lib);
    s.lib = lib;
    ui_mark(UI_LIBRARY);
}

/* The name shows at once, the next dump confirms it. */
static void show_picked(uint8_t source, const char *name)
{
    nano_state_t *st = &g_app.state;
    if (source == NANO_SOURCE_IR) {
        st->cab_on = true;
        strlcpy(st->ir_short_name, name, sizeof(st->ir_short_name));
    } else {
        st->capture_on = true;
        strlcpy(st->capture_name, name, sizeof(st->capture_name));
    }
    ui_mark(UI_STATE);
    link_schedule_state(CONFIRM_MS);
}

/* Cortex Cloud's preview: a live edit of the preset (kept when it is saved). Its "Use" (writing the item into a bank
 * slot, nano_build_*_slot_load) is left out on purpose: other presets on that slot would change with it. */
void library_pick(uint8_t source, uint8_t list, uint16_t index, const char *name)
{
    if (!g_app.link_ready || !g_app.state_valid) return;
    uint8_t f[NANO_FRAME_MAX];
    size_t n = source == NANO_SOURCE_IR ? nano_build_cab_load(f, sizeof(f), list, index, name)
                                        : nano_build_capture_preview(f, sizeof(f), list, index, name);
    if (!app_send(f, n)) return;
    ESP_LOGI(TAG, "-> load %s %s %u \"%s\"", list_name(list), source_name(source), index + 1, name);
    show_picked(source, name);
}

bool library_on_reply(int msg_type, const uint8_t *payload, size_t len)
{
    uint64_t ok = 0;
    switch (msg_type) {
    case NANO_MSG_LIBRARY:
        on_library(payload, len);
        return true;
    case NANO_MSG_CAPTURE_PREVIEWED:
        nano_get_varint(payload, len, 3, &ok);
        if (ok != 1) ESP_LOGW(TAG, "<- capture preview refused");
        return true;
    default:
        return false;
    }
}

void library_link_reset(void)
{
    s.asked = false;
    free(s.lib);
    s.lib = NULL;
    ui_unmark(UI_LIBRARY);
}

void library_ui_push(uint32_t parts)
{
    if (!(parts & UI_LIBRARY) || !s.lib) return;
    nano_ui_set_library(s.source, s.lib); /* the screen owns it now */
    s.lib = NULL;
}
