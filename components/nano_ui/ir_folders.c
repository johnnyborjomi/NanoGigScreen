#include "nano_ir_folders.h"

#include <string.h>
#include <strings.h>

/* FNV-1a over the list and the name. */
uint32_t nano_ir_folder_key(int list, const char *ir_name)
{
    uint32_t h = 2166136261u ^ (uint32_t)list;
    h *= 16777619u;
    for (const char *p = ir_name; *p; p++) {
        h ^= (uint8_t)*p;
        h *= 16777619u;
    }
    return h;
}

const nano_ir_folder_t *nano_ir_folder_find(const nano_ir_folders_t *f, uint8_t id)
{
    for (int i = 0; id && i < f->count; i++) {
        if (f->folders[i].id == id) return &f->folders[i];
    }
    return NULL;
}

static int filed_at(const nano_ir_folders_t *f, uint32_t key)
{
    for (int i = 0; i < f->filed_count; i++) {
        if (f->filed_ir[i] == key) return i;
    }
    return -1;
}

uint8_t nano_ir_folder_of(const nano_ir_folders_t *f, int list, const char *ir_name)
{
    int i = filed_at(f, nano_ir_folder_key(list, ir_name));
    return i < 0 ? NANO_IR_NO_FOLDER : f->filed_folder[i];
}

uint8_t nano_ir_folder_named(const nano_ir_folders_t *f, int list, const char *name, uint8_t except)
{
    for (int i = 0; i < f->count; i++) {
        const nano_ir_folder_t *d = &f->folders[i];
        if (d->list == list && d->id != except && strcasecmp(d->name, name) == 0) return d->id;
    }
    return NANO_IR_NO_FOLDER;
}

static void unfile_at(nano_ir_folders_t *f, int i)
{
    f->filed_count--;
    f->filed_ir[i] = f->filed_ir[f->filed_count];
    f->filed_folder[i] = f->filed_folder[f->filed_count];
}

static bool name_ok(const nano_ir_folders_t *f, int list, const char *name, uint8_t except)
{
    size_t n = name ? strlen(name) : 0;
    return n >= 1 && n <= NANO_IR_FOLDER_NAME_MAX && !nano_ir_folder_named(f, list, name, except);
}

/* The next id no folder uses (ids wrap past 255, skipping 0). */
static uint8_t new_id(nano_ir_folders_t *f)
{
    for (int tries = 0; tries < 256; tries++) {
        uint8_t id = ++f->next_id;
        if (id != NANO_IR_NO_FOLDER && !nano_ir_folder_find(f, id)) return id;
    }
    return NANO_IR_NO_FOLDER;
}

bool nano_ir_folder_apply(nano_ir_folders_t *f, nano_ir_folder_op_t op, int list, uint8_t folder, const char *text)
{
    nano_ir_folder_t *d = (nano_ir_folder_t *)nano_ir_folder_find(f, folder);
    switch (op) {
    case NANO_IR_FOLDER_CREATE: {
        if (f->count >= NANO_IR_FOLDERS_MAX || !name_ok(f, list, text, NANO_IR_NO_FOLDER)) return false;
        uint8_t id = new_id(f);
        if (id == NANO_IR_NO_FOLDER) return false;
        nano_ir_folder_t *n = &f->folders[f->count++];
        memset(n, 0, sizeof(*n));
        n->id = id;
        n->list = (uint8_t)list;
        strncpy(n->name, text, NANO_IR_FOLDER_NAME_MAX);
        return true;
    }
    case NANO_IR_FOLDER_RENAME:
        if (!d || !name_ok(f, d->list, text, d->id)) return false;
        memset(d->name, 0, sizeof(d->name));
        strncpy(d->name, text, NANO_IR_FOLDER_NAME_MAX);
        return true;
    case NANO_IR_FOLDER_DELETE: {
        if (!d) return false;
        for (int i = f->filed_count - 1; i >= 0; i--) {
            if (f->filed_folder[i] == folder) unfile_at(f, i);
        }
        int at = (int)(d - f->folders);
        memmove(&f->folders[at], &f->folders[at + 1], (size_t)(f->count - at - 1) * sizeof(f->folders[0]));
        f->count--;
        return true;
    }
    case NANO_IR_FOLDER_FILE: {
        if (!text || !text[0] || (folder != NANO_IR_NO_FOLDER && (!d || d->list != list))) return false;
        uint32_t key = nano_ir_folder_key(list, text);
        int i = filed_at(f, key);
        if (folder == NANO_IR_NO_FOLDER) {
            if (i >= 0) unfile_at(f, i);
            return true;
        }
        if (i < 0) {
            if (f->filed_count >= NANO_IR_FILED_MAX) return false;
            i = f->filed_count++;
            f->filed_ir[i] = key;
        }
        f->filed_folder[i] = folder;
        return true;
    }
    }
    return false;
}
