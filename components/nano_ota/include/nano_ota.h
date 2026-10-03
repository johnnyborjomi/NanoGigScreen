/*
 * Firmware updates over Wi-Fi: join a network (credentials kept in NVS), read the header of the
 * image at CONFIG_NANOGIG_OTA_URL, install it into the other OTA slot when its version differs
 * from the running one, restart.
 *
 * Wi-Fi only runs in update mode: the app shuts Bluetooth down first (no PSRAM: the heap cannot
 * hold both stacks plus TLS), and leaving update mode restarts the screen.
 *
 * Commands may be called from any task (the LVGL task included): they post to the update task
 * and return. Events arrive on the update task: copy and return, take the LVGL lock to show them.
 */
#ifndef NANO_OTA_H
#define NANO_OTA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NANO_OTA_MAX_NETWORKS 10

typedef struct {
    char ssid[33];
    int8_t rssi;
    bool secure;
} nano_ota_network_t;

typedef enum {
    NANO_OTA_EV_SCANNING,
    NANO_OTA_EV_SCAN_DONE,       /* networks / network_count */
    NANO_OTA_EV_NO_WIFI,         /* check asked for, no saved network: pick one */
    NANO_OTA_EV_CONNECTING,      /* text = SSID */
    NANO_OTA_EV_WIFI_SAVED,      /* joined: text = SSID, now remembered */
    NANO_OTA_EV_CHECKING,
    NANO_OTA_EV_UP_TO_DATE,      /* text = version */
    NANO_OTA_EV_AVAILABLE,       /* text = the new version */
    NANO_OTA_EV_PROGRESS,        /* percent 0..100 */
    NANO_OTA_EV_INSTALLED,       /* restarting in ~2 s */
    NANO_OTA_EV_ERROR,           /* text = what went wrong, short */
} nano_ota_event_kind_t;

typedef struct {
    nano_ota_event_kind_t kind;
    char text[48];
    int percent;
    const nano_ota_network_t *networks; /* SCAN_DONE only; valid until the next scan */
    int network_count;
} nano_ota_event_t;

typedef void (*nano_ota_cb_t)(const nano_ota_event_t *ev);

/* Boot: the command queue (taps during the Bluetooth shutdown wait there, not dropped). */
int nano_ota_init(void);
/* Boot: report the running version and keep this image (cancels a pending rollback). */
const char *nano_ota_running_version(void);
void nano_ota_mark_valid(void);
/* SSID of the remembered network into `out` (false = none). */
bool nano_ota_saved_ssid(char *out, size_t cap);

/* Update mode: start Wi-Fi and the update task. Bluetooth must be shut down already. Once. */
int nano_ota_start(nano_ota_cb_t cb);
void nano_ota_scan(void);
/* Join and remember a network (empty password = open), then check. */
void nano_ota_join(const char *ssid, const char *password);
/* Join the remembered network if needed, then read the image header. */
void nano_ota_check(void);
/* Download and install the image found by the last check, then restart. */
void nano_ota_install(void);

#ifdef __cplusplus
}
#endif
#endif
