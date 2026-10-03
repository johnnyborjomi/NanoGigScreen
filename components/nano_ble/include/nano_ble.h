/*
 * NimBLE GATT client for the Nano Cortex: scan for the pedal, connect,
 * request a 517-byte MTU, subscribe to c305 (notify only: c306 is an
 * indicate mirror that throttles the pitch stream), write commands to c304.
 * Reconnects forever after a link loss (a pedal power cycle takes ~13 s).
 *
 * Callbacks run on the NimBLE host task: copy the data out and return.
 */
#ifndef NANO_BLE_H
#define NANO_BLE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    NANO_BLE_IDLE = 0,
    NANO_BLE_SCANNING,
    NANO_BLE_CONNECTING,   /* GATT connect + discovery + subscribe in progress */
    NANO_BLE_READY,        /* c305 subscribed, c304 writable */
} nano_ble_status_t;

typedef struct {
    void (*on_status)(nano_ble_status_t status, const char *detail);
    /* A c305 notification (one packet, <= MTU-3 bytes). */
    void (*on_notify)(const uint8_t *data, size_t len);
} nano_ble_callbacks_t;

/* Initialise the controller + host and start scanning. Call once. */
int nano_ble_start(const nano_ble_callbacks_t *cb);
nano_ble_status_t nano_ble_status(void);
/* Write a command frame to c304 (with response). 0 on success, else a NimBLE error code. */
int nano_ble_write(const uint8_t *data, size_t len);
/*
 * Enable (scan + connect, the default) or disable the link: disconnects and stops scanning so
 * Cortex Cloud can take the pedal without powering the screen off. Callable from any task.
 */
void nano_ble_set_enabled(bool enabled);
bool nano_ble_enabled(void);
/* Negotiated ATT MTU (0 until connected). */
uint16_t nano_ble_mtu(void);
/*
 * Firmware update mode: drop the link, stop the host, deinit the controller and hand its memory
 * to the heap (Wi-Fi + TLS need it). One way: Bluetooth comes back only after a restart. Blocks for
 * up to ~1 s; call from an app task, never from a NimBLE callback.
 */
void nano_ble_shutdown(void);

#ifdef __cplusplus
}
#endif
#endif
