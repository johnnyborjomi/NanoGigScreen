/*
 * Folders: our own grouping of the pedal's IRs and captures (Cortex Cloud has none; the pedal knows nothing of them).
 * A folder belongs to one list (NANO_FOLDER_LIST: user or factory IRs or captures), an item sits in at most one
 * folder or at the top of its list. Items are remembered by a hash of list + name, so a folder keeps them when items
 * are added or removed in Cortex Cloud (the pedal's index shifts, the name does not). Fixed size (~1 KB), stored as
 * one blob by the app; IRs and captures share its 16 folders and 160 filed items.
 *
 * The app owns the folders and applies every change (nano_ui's on_ir_folder_edit); the list page shows a copy.
 */
#ifndef NANO_IR_FOLDERS_H
#define NANO_IR_FOLDERS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NANO_IR_FOLDERS_MAX 16
#define NANO_IR_FOLDER_NAME_MAX 16
#define NANO_IR_FILED_MAX 160
#define NANO_IR_NO_FOLDER 0      /* folder ids are 1..255 */
/* The lists folders belong to: IRs 0 / 1 (NANO_IR_FACTORY / NANO_IR_USER, as before captures had folders), captures
 * 2 / 3. `source` = nano_source_t. */
#define NANO_FOLDER_LIST(source, list) ((source) == 0 ? 2 + (list) : (list))

typedef struct {
    uint8_t id;                  /* stable: what the filed IRs point at */
    uint8_t list;                /* NANO_FOLDER_LIST */
    char name[NANO_IR_FOLDER_NAME_MAX + 1];
} nano_ir_folder_t;

typedef struct {
    uint8_t count;               /* folders, in creation order */
    uint8_t next_id;
    nano_ir_folder_t folders[NANO_IR_FOLDERS_MAX];
    uint16_t filed_count;
    uint32_t filed_ir[NANO_IR_FILED_MAX];     /* nano_ir_folder_key */
    uint8_t filed_folder[NANO_IR_FILED_MAX];
} nano_ir_folders_t;

/* What the IR list page asks the app to do (on_ir_folder_edit). */
typedef enum {
    NANO_IR_FOLDER_CREATE = 0,   /* list, text = name */
    NANO_IR_FOLDER_RENAME,       /* folder, text = name */
    NANO_IR_FOLDER_DELETE,       /* folder: its IRs go back to the top of the list */
    NANO_IR_FOLDER_FILE,         /* list, text = IR name, folder (NANO_IR_NO_FOLDER = the top of the list) */
} nano_ir_folder_op_t;

uint32_t nano_ir_folder_key(int list, const char *ir_name);
/* Folder by id, NULL when none. */
const nano_ir_folder_t *nano_ir_folder_find(const nano_ir_folders_t *f, uint8_t id);
/* The folder an IR is in, NANO_IR_NO_FOLDER when none. */
uint8_t nano_ir_folder_of(const nano_ir_folders_t *f, int list, const char *ir_name);
/* A folder of `list` named `name` (letter case ignored), other than `except`: its id, else NANO_IR_NO_FOLDER. */
uint8_t nano_ir_folder_named(const nano_ir_folders_t *f, int list, const char *name, uint8_t except);

/* Changes; false when refused (full, unknown folder, name in use or too long). */
bool nano_ir_folder_apply(nano_ir_folders_t *f, nano_ir_folder_op_t op, int list, uint8_t folder, const char *text);

#ifdef __cplusplus
}
#endif
#endif
