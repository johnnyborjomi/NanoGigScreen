/*
 * The IR tab (Cortex Cloud's IR loader): Level, High pass, Low pass, phase, and a factory IR's microphone and
 * position. Frames and scales in nano_build.h / nano_scales.h; the read / match / show cycle is a remote page.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "nano_scales.h"

void ir_page_init(void);
void ir_page_set_open(bool open);
void ir_page_set_param(nano_cab_param_t param, float normalized);
void ir_page_set_phase(bool inverted);
void ir_page_set_mic(uint8_t position, const char *mic);
