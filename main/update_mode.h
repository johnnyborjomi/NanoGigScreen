/*
 * Firmware update (Settings page 3): Bluetooth shuts down, Wi-Fi starts, the screen fetches CONFIG_NANOGIG_OTA_URL
 * into the other OTA slot; closing the page restarts. A new image is kept once it links to the pedal, runs a minute
 * or enters update mode; one that crashes before that is rolled back by the bootloader on the next start.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Bluetooth off, the pedal's buffers freed for download headroom, Wi-Fi on. Until the restart. */
void update_mode_enter(void);
bool update_mode_active(void);
void update_mode_keep_image(void);
void update_mode_tick(int64_t now);
