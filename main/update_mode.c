#include "update_mode.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_system.h"
#include "nano_ble.h"
#include "nano_ota.h"
#include "nano_ui.h"
#include "pedal_in.h"

static const char *TAG = "update";

#define KEEP_IMAGE_AFTER_US (60 * 1000 * 1000)

static bool s_active;
static bool s_image_kept; /* the running image is marked valid (no rollback) */

bool update_mode_active(void) { return s_active; }

void update_mode_keep_image(void)
{
    if (s_image_kept) return;
    s_image_kept = true;
    nano_ota_mark_valid();
}

void update_mode_tick(int64_t now)
{
    if (!s_image_kept && now > KEEP_IMAGE_AFTER_US) update_mode_keep_image();
}

/* Update task -> update view. Waits for the display as long as it takes: the LVGL task never waits on the update
 * task (its commands are posted without blocking), and a lost INSTALLED / ERROR would leave the page hanging. */
static void on_ota_event(const nano_ota_event_t *ev)
{
    if (!lvgl_port_lock(0)) return;
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

void update_mode_enter(void)
{
    if (s_active) return;
    s_active = true;
    ESP_LOGI(TAG, "update mode: Bluetooth off, Wi-Fi on");
    update_mode_keep_image(); /* the next image can only be written once this one is no longer pending */
    nano_ble_shutdown();
    /* The pedal is gone until the restart: its buffers (~27 KB) become download headroom. */
    pedal_in_release();
    ESP_LOGI(TAG, "pedal buffers released, free heap %u B", (unsigned)esp_get_free_heap_size());
    if (nano_ota_start(on_ota_event) != 0) {
        if (lvgl_port_lock(0)) {
            nano_ui_update_status(NANO_UPDATE_ERROR, "Wi-Fi failed to start", 0);
            lvgl_port_unlock();
        }
        return;
    }
    nano_ota_check(); /* asks for a network first when none is remembered */
}
