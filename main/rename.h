/*
 * Preset rename (RenamePreset, verified 2026-10-08): the pedal stores the name at once and answers type 0x70.
 * The cached name changes only when it says yes; the rename page gets an answer either way (yes, no, no answer in
 * 3 s, link lost).
 */
#pragma once

#include <stdint.h>

#include "nano_decode.h"

void rename_preset(uint8_t index, const char *name);
void rename_on_reply(const nano_event_t *ev);
void rename_tick(int64_t now);
void rename_link_reset(void);
void rename_ui_push(uint32_t parts);
