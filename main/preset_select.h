/*
 * Preset select over Bluetooth (c304, confirmed by the state dump's field 13). The target shows at once; rapid taps
 * are coalesced: one select in flight at a time, the latest target goes out on its ack (the pedal would load every
 * preset skipped past, ~150 ms each). A dump that still shows the old preset does not undo the target.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "nano_decode.h"

void preset_select_step(int delta);   /* prev / next, from the target still in flight */
void preset_select_index(int index);
/* A select is unconfirmed (in flight, or a target waiting for its dump); with `settle`, also the 1.5 s after one,
 * while the pedal may still be loading. */
bool preset_select_busy(bool settle);
/* Before a dump is shown: keep the target while the dump is for an earlier select. */
void preset_select_filter(nano_state_t *dump);
void preset_select_on_changed(const nano_event_t *ev); /* 0x1D: the pedal changed preset */
void preset_select_on_ack(void);
void preset_select_on_bypass_notice(void);             /* 0x1F, which also follows our select */
void preset_select_tick(int64_t now);
void preset_select_link_reset(void);
