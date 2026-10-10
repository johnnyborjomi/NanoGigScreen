/*
 * One-tap edits shown on the gig view and the Capture / IR page: FX blocks and the gate on / off, an FX block's model,
 * capture volume, the IR on / off and its slot on the pedal's IR list.
 * Optimistic: the screen shows the edit at once; a dump requested before the edit was written cannot undo it, one
 * requested after confirms it. All are live edits (the preset is not saved).
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "nano_decode.h"

void block_edits_toggle_fx(uint8_t slot, bool currently_on);
/* Another model in FX slot `slot` (a type from nano_fx_params.h; the model's defaults, as in Cortex Cloud). */
void block_edits_set_fx_model(uint8_t slot, uint32_t type);
void block_edits_toggle_gate(bool currently_on);
/* Gate threshold, raw 0..255 (the gate page already shows it). */
void block_edits_set_gate_threshold(uint8_t raw);
void block_edits_set_capture_volume(uint8_t raw);
/* IR off, or back on (the slot the preset last had). */
void block_edits_set_ir_on(bool on);
/* The previous (-1) / next (+1) IR on the pedal's list. */
void block_edits_step_ir(int delta);
/* Before a dump is shown: keep what was written after `requested_us` (the dump's request). */
void block_edits_filter(nano_state_t *dump, int64_t requested_us);
