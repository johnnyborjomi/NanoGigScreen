#include "settings.h"

#include <string.h>

#include "app.h"
#include "cyd_board.h"
#include "esp_log.h"
#include "nvs.h"

static const char *TAG = "settings";

#define NVS_NAMESPACE "nanogig"
#define KEY_META "meta"
#define KEY_BANK "bank"
#define KEY_LABEL_STYLE "lstyle"
#define KEY_EXP_SHOW "expshow"
#define KEY_ROTATE "rot"           /* 1 = display turned 180 degrees */
#define KEY_BRIGHTNESS "bright"    /* 1..10 */
#define META_MAGIC 0x4E474D31u     /* "NGM1" */
#define BRIGHTNESS_SAVE_DELAY_US (1000 * 1000)

static struct {
    uint8_t bank_size, label_style, brightness;
    bool exp_show, rotate_180;
    int64_t brightness_save_us; /* 0 = saved */
} s;

/* One u8 with a default (missing key or unreadable NVS = the default). */
static uint8_t load_u8(const char *key, uint8_t dflt)
{
    nvs_handle_t h;
    uint8_t v = dflt;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) == ESP_OK) {
        nvs_get_u8(h, key, &v);
        nvs_close(h);
    }
    return v;
}

static void save_u8(const char *key, uint8_t v)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) return;
    uint8_t old = 0xff;
    nvs_get_u8(h, key, &old);
    if (old != v && nvs_set_u8(h, key, v) == ESP_OK) nvs_commit(h);
    nvs_close(h);
}

/* The cache blob: a magic in front, so a cache from another layout reads as empty. */
typedef struct {
    uint32_t magic;
    nano_metadata_t meta;
} meta_blob_t;

static meta_blob_t s_blob; /* ~7 KB, the one copy: g_app.meta points into it */

static void load_meta(void)
{
    g_app.meta = &s_blob.meta;
    g_app.meta_valid = false;
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) == ESP_OK) {
        size_t len = sizeof(s_blob);
        esp_err_t err = nvs_get_blob(h, KEY_META, &s_blob, &len);
        nvs_close(h);
        g_app.meta_valid = err == ESP_OK && len == sizeof(s_blob) && s_blob.magic == META_MAGIC;
    }
    if (!g_app.meta_valid) memset(&s_blob, 0, sizeof(s_blob));
    ESP_LOGI(TAG, "metadata cache %s", g_app.meta_valid ? "loaded" : "empty");
}

void settings_save_meta(void)
{
    s_blob.magic = META_MAGIC;
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_set_blob(h, KEY_META, &s_blob, sizeof(s_blob));
        if (err == ESP_OK) err = nvs_commit(h);
        nvs_close(h);
    }
    ESP_LOGI(TAG, "metadata cache %s", err == ESP_OK ? "saved" : "save failed");
}

void settings_load(void)
{
    load_meta();
    uint8_t v = load_u8(KEY_BANK, 4);
    s.bank_size = v >= 2 && v <= 8 ? v : 4;
    v = load_u8(KEY_LABEL_STYLE, 0);
    s.label_style = v <= NANO_LABEL_NUMERIC ? v : 0;
    s.exp_show = load_u8(KEY_EXP_SHOW, 1) != 0;
    s.rotate_180 = load_u8(KEY_ROTATE, cyd_display_rotation() ? 1 : 0) != 0;
    v = load_u8(KEY_BRIGHTNESS, CYD_BRIGHTNESS_MAX);
    s.brightness = v < CYD_BRIGHTNESS_MIN ? CYD_BRIGHTNESS_MIN : v > CYD_BRIGHTNESS_MAX ? CYD_BRIGHTNESS_MAX : v;
}

uint8_t settings_bank_size(void) { return s.bank_size; }
uint8_t settings_label_style(void) { return s.label_style; }
bool settings_exp_show(void) { return s.exp_show; }
bool settings_rotate_180(void) { return s.rotate_180; }
uint8_t settings_brightness(void) { return s.brightness; }

void settings_set_bank_size(uint8_t v)
{
    s.bank_size = v;
    save_u8(KEY_BANK, v);
}

void settings_set_label_style(uint8_t v)
{
    s.label_style = v;
    save_u8(KEY_LABEL_STYLE, v);
}

void settings_set_exp_show(bool show)
{
    s.exp_show = show;
    save_u8(KEY_EXP_SHOW, show);
}

void settings_set_rotation(bool rotate_180)
{
    ESP_LOGI(TAG, "display rotation %s", rotate_180 ? "180" : "0");
    s.rotate_180 = rotate_180;
    save_u8(KEY_ROTATE, rotate_180 ? 1 : 0);
    ui_mark(UI_ROTATION);
}

void settings_set_brightness(uint8_t v)
{
    cyd_backlight_set_level(v); /* instant; the value label already shows it */
    s.brightness = v;
    s.brightness_save_us = app_now_us() + BRIGHTNESS_SAVE_DELAY_US; /* one flash write once the slider rests */
}

void settings_flush(void)
{
    if (!s.brightness_save_us) return;
    s.brightness_save_us = 0;
    save_u8(KEY_BRIGHTNESS, s.brightness);
}

void settings_tick(int64_t now)
{
    if (s.brightness_save_us && now >= s.brightness_save_us) settings_flush();
}

void settings_ui_push(uint32_t parts)
{
    if (parts & UI_ROTATION) cyd_display_set_rotation(s.rotate_180); /* flips the panel and the touch map, redraws */
}
