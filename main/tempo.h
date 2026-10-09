/*
 * Tempo: the tempo view's steps and the pedal's tap tempo mode. Set by mirroring the pedal's 0x91 per-tap message
 * (verified 2026-09-26). Steps are coalesced: instant on screen, one write per 80 ms with the latest value, one
 * confirming dump once the presses settle.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "nano_decode.h"

void tempo_step(int delta);
void tempo_view(bool open);                       /* the screen's tempo view opened / closed: tap mode on / off */
void tempo_on_tap(const nano_event_t *ev);        /* the pedal's tap tempo: opens / closes the view */
void tempo_filter(nano_state_t *dump);            /* before a dump is shown: one from before the last press */
void tempo_tick(int64_t now);
void tempo_link_reset(void);
