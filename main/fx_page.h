/*
 * The FX editor (Cortex Cloud's FX block page): one FX block's parameters, read while its editor shows and the block
 * is on, and written live. The model change is a block edit (block_edits_set_fx_model). Frames in nano_build.h,
 * the parameters per model in nano_fx_params.h; the read / match / show cycle is a remote page.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

void fx_page_init(void);
/* The editor of FX slot `slot` (0..4) opened, or closed. */
void fx_page_set_open(uint8_t slot, bool open);
void fx_page_set_param(uint8_t slot, uint8_t param, float normalized);
