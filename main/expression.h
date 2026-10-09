/*
 * Expression pedal: its position and values (~20 events a second while it moves) and the shown preset's
 * assignments (Cortex Cloud's 0x3C read). The 0x3D reply carries no preset number, so the request remembers which
 * preset it asked about; the tile tracks only show assignments that belong to the shown preset.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "nano_decode.h"

void expression_set_show(bool show);   /* setting: draw the indicators */
void expression_on_preset_change(void); /* a new preset: its assignments get their own tries */
void expression_on_position(const nano_event_t *ev);
void expression_on_values(const nano_event_t *ev);
void expression_on_assignments(const nano_event_t *ev);
void expression_tick(int64_t now);
void expression_link_reset(void);
void expression_ui_push(uint32_t parts);
