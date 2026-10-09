/*
 * The IR folders (nano_ir_folders.h): kept in flash, changed only here (the IR list page asks), pushed to the
 * screen while the list shows.
 */
#pragma once

#include <stdint.h>

#include "nano_ir_folders.h"

void ir_folder_store_load(void);   /* once, after settings_load */
void ir_folder_store_edit(nano_ir_folder_op_t op, uint8_t list, uint8_t folder, const char *text);
void ir_folder_store_ui_push(uint32_t parts);
