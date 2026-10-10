/*
 * The capture and IR lists: every capture / IR on the pedal (factory and user), read when a list opens, and loading
 * one into the current preset (Cortex Cloud's preview). The list lives in the screen while the page shows (handed
 * over on the reply), nowhere else.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* `source` = NANO_SOURCE_CAPTURE / NANO_SOURCE_IR (nano_ui.h). */
void library_set_open(uint8_t source, bool open);
/* `list` = NANO_IR_FACTORY / NANO_IR_USER, `index` its position there, `name` as the library gave it. */
void library_pick(uint8_t source, uint8_t list, uint16_t index, const char *name);
/* A reply of type `msg_type`: true when it was the library's or a capture load's. */
bool library_on_reply(int msg_type, const uint8_t *payload, size_t len);
void library_link_reset(void);
void library_ui_push(uint32_t parts);
