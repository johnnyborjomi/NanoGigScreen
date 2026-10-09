/*
 * The IR list page: every IR on the pedal (factory and user), read when the page opens, and loading one into the
 * current preset. The list lives in the screen while the page shows (handed over on the reply), nowhere else.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void ir_library_set_open(bool open);
/* `list` = NANO_IR_FACTORY / NANO_IR_USER, `index` its position there, `name` as the library gave it. */
void ir_library_pick(uint8_t list, uint16_t index, const char *name);
/* A reply of type `msg_type`: true when it was the library. */
bool ir_library_on_reply(int msg_type, const uint8_t *payload, size_t len);
void ir_library_link_reset(void);
void ir_library_ui_push(uint32_t parts);
