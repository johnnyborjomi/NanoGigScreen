#include "ir_folder_store.h"

#include <string.h>

#include "app.h"
#include "esp_log.h"
#include "nano_ui.h"
#include "settings.h"

static const char *TAG = "folders";

#define KEY_FOLDERS "irfolders"
#define FOLDERS_MAGIC 0x4E474631u /* "NGF1" */

static struct {
    uint32_t magic;
    nano_ir_folders_t f;
} s_blob;

void ir_folder_store_load(void)
{
    bool ok = settings_load_blob(KEY_FOLDERS, &s_blob, sizeof(s_blob)) && s_blob.magic == FOLDERS_MAGIC && s_blob.f.count <= NANO_IR_FOLDERS_MAX &&
              s_blob.f.filed_count <= NANO_IR_FILED_MAX;
    if (!ok) memset(&s_blob, 0, sizeof(s_blob));
    ESP_LOGI(TAG, "%u IR folders, %u IRs filed", s_blob.f.count, s_blob.f.filed_count);
}

void ir_folder_store_edit(nano_ir_folder_op_t op, uint8_t list, uint8_t folder, const char *text)
{
    static const char *const OPS[] = { "create", "rename", "delete", "file" };
    bool ok = nano_ir_folder_apply(&s_blob.f, op, list, folder, text);
    ESP_LOGI(TAG, "%s (list %u, folder %u, \"%s\")%s", OPS[op & 3], list, folder, text, ok ? "" : ": refused");
    if (ok) {
        s_blob.magic = FOLDERS_MAGIC;
        if (!settings_save_blob(KEY_FOLDERS, &s_blob, sizeof(s_blob))) ESP_LOGW(TAG, "save failed");
    }
    ui_mark(UI_IR_FOLDERS); /* refused too: the page drops what it showed */
}

void ir_folder_store_ui_push(uint32_t parts)
{
    if (parts & UI_IR_FOLDERS) nano_ui_set_ir_folders(&s_blob.f);
}
