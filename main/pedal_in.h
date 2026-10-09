/*
 * Everything the pedal sends: notification packets are reassembled into messages (nano_assembler), decoded, and
 * routed to the module that owns them. Dumps become the shown state (after each module had its say about edits
 * the dump may predate) or the metadata cache.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool pedal_in_init(void);   /* allocates the reassembly buffers; false = out of memory */
void pedal_in_packet(const uint8_t *data, size_t len);
void pedal_in_tick(void);   /* flushes a message whose last packet never came */
void pedal_in_link_reset(void);
void pedal_in_release(void); /* update mode: the buffers become download headroom; packets are dropped from here on */
