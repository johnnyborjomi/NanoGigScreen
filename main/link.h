/*
 * The session with the pedal: link up / down, the state / metadata / device settings reads, outputs mute.
 *
 * Sync rules (from NanoGig's engine, hardware-verified on NanOS 2.2.1):
 *   connect -> state dump (fast, ~0.3 s) -> metadata dump only when the flash cache is missing or contradicts the
 *   state (the ~17 KB dump takes ~6 s and queues every footswitch event behind it) -> device settings.
 *   A pedal change notice -> state re-read (link_schedule_state, app.h); requests keep a minimum gap, and each dump
 *   is matched to the request it answers so optimistic edits know whether it can have seen them.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "nano_ble.h"
#include "nano_decode.h"

void link_on_status(nano_ble_status_t status, const char *detail);
/* Send time of the state request the arriving dump answers (the oldest outstanding one). */
int64_t link_pop_request_time(void);
void link_request_state(void);
/* After each dump: read the metadata when the cache contradicts it (not while a select settles), else the
 * device settings once per link. */
void link_on_state(const nano_state_t *dump, bool switching);
bool link_metadata_requested(void); /* only a reply to our own request can be metadata */
void link_on_metadata(void);        /* g_app.meta was replaced */
void link_on_settings(bool outputs_muted);
void link_on_outputs_mute_ack(void);
void link_set_outputs_mute(bool mute);
void link_tick(int64_t now);
void link_ui_push(uint32_t parts);
