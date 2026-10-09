/*
 * Tuner: on `0F C0 20 01 2D <f32 Hz> 30 01 38 <mute> 7F 00 00 00` / off `06 C0 20 00 7F 00 00 00` (2026-09-19).
 * Opened from the menu (we send tuner-on) or from the pedal's footswitch (its report / readings open the view).
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "nano_decode.h"

void tuner_set(bool on);
void tuner_set_mute(bool mute);            /* re-sends tuner-on with the new flag, as Cortex Cloud does */
void tuner_on_pitch(const nano_event_t *ev);
void tuner_on_report(const nano_event_t *ev);
void tuner_tick(int64_t now);
void tuner_link_reset(void);
void tuner_ui_push(uint32_t parts);
