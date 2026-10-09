/*
 * One-tap edits shown on the gig view and the capture page: FX blocks and the gate on / off, capture volume.
 * Optimistic: the screen shows the edit at once; a dump requested before the edit was written cannot undo it, one
 * requested after confirms it. All are live edits (the preset is not saved).
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "nano_decode.h"

void block_edits_toggle_fx(uint8_t slot, bool currently_on);
void block_edits_toggle_gate(bool currently_on);
void block_edits_set_capture_volume(uint8_t raw);
/* Before a dump is shown: keep what was written after `requested_us` (the dump's request). */
void block_edits_filter(nano_state_t *dump, int64_t requested_us);
