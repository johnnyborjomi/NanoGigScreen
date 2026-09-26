/*
 * Render the gig screen on the host. Each scenario ends up as out/<name>.png
 * (written as PPM, converted with macOS `sips`). Uses the real hardware
 * fixtures from the protocol tests so the tiles show real models.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "fixtures.h"
#include "lvgl.h"
#include "nano_decode.h"
#include "nano_ui.h"

#define W 320
#define H 240

static uint16_t fb[W * H];
static uint32_t tick;

static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    const uint16_t *src = (const uint16_t *)px_map;
    for (int32_t y = area->y1; y <= area->y2; y++) {
        for (int32_t x = area->x1; x <= area->x2; x++) fb[y * W + x] = *src++;
    }
    lv_display_flush_ready(disp);
}

static uint32_t tick_cb(void) { return tick; }

static void render(int ms)
{
    for (int i = 0; i < ms / 10; i++) {
        tick += 10;
        lv_timer_handler();
    }
}

static void save(const char *dir, const char *name)
{
    char ppm[512], png[512];
    snprintf(ppm, sizeof(ppm), "%s/%s.ppm", dir, name);
    snprintf(png, sizeof(png), "%s/%s.png", dir, name);
    FILE *f = fopen(ppm, "wb");
    if (!f) { perror(ppm); exit(1); }
    fprintf(f, "P6\n%d %d\n255\n", W, H);
    for (int i = 0; i < W * H; i++) {
        uint16_t p = fb[i];
        uint8_t rgb[3] = { (uint8_t)(((p >> 11) & 0x1f) * 255 / 31), (uint8_t)(((p >> 5) & 0x3f) * 255 / 63), (uint8_t)((p & 0x1f) * 255 / 31) };
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
    char cmd[1200];
    snprintf(cmd, sizeof(cmd), "sips -s format png '%s' --out '%s' >/dev/null 2>&1 && rm '%s'", ppm, png, ppm);
    if (system(cmd) != 0) fprintf(stderr, "sips failed for %s\n", name);
    printf("wrote %s\n", png);
}

static size_t from_hex(const char *text, uint8_t *out, size_t cap)
{
    size_t n = 0; int hi = -1;
    for (const char *p = text; *p; p++) {
        int v;
        if (*p >= '0' && *p <= '9') v = *p - '0';
        else if (*p >= 'A' && *p <= 'F') v = *p - 'A' + 10;
        else if (*p >= 'a' && *p <= 'f') v = *p - 'a' + 10;
        else continue;
        if (hi < 0) { hi = v; continue; }
        if (n >= cap) return 0;
        out[n++] = (uint8_t)(hi * 16 + v); hi = -1;
    }
    return n;
}

static void noop(void) {}
static void noop_fx(uint8_t s, bool on) { (void)s; (void)on; }
static void noop_b(bool b) { (void)b; }
static void noop_u8(uint8_t v) { (void)v; }
static void noop_i(int v) { (void)v; }

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "out";
    mkdir(dir, 0755);
    lv_init();
    lv_tick_set_cb(tick_cb);
    lv_display_t *disp = lv_display_create(W, H);
    static uint16_t buf[W * 40]; /* partial mode, like the device: flush gets tight area buffers */
    lv_display_set_buffers(disp, buf, NULL, sizeof(buf), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(disp, flush_cb);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);

    nano_ui_callbacks_t cb = { .on_prev_preset = noop, .on_next_preset = noop, .on_toggle_fx = noop_fx, .on_toggle_gate = noop_b, .on_tuner = noop_b, .on_tuner_mute = noop_b, .on_tempo_delta = noop_i, .on_tempo_view = noop_b, .on_link = noop_b, .on_bank_size = noop_u8 };
    nano_ui_create(disp, &cb);

    static nano_metadata_t meta;
    memset(&meta, 0, sizeof(meta));
    strcpy(meta.presets[14].name, "Fender Prnc Clean");
    strcpy(meta.presets[14].capture_name, "CA John's Ch1 1");
    strcpy(meta.presets[3].name, "5150 Lead");
    strcpy(meta.presets[3].capture_name, "EVH 5150III Ch3 Gain3");
    strcpy(meta.presets[0].name, "Stealth Gojira Blue!");
    strcpy(meta.presets[0].capture_name, "Stealth EL34 Gojira Blue");
    strcpy(meta.presets[33].name, "Prince");
    meta.preset_record_count = 64;

    uint8_t pkt[512];
    nano_state_t st;

    /* 0. boot: nothing known yet */
    nano_ui_set_status("Looking for the pedal", false);
    nano_ui_set_stale(true);
    render(200);
    save(dir, "0-boot");

    /* 1. connected, short name (real dump: preset 14) */
    nano_ui_set_status("Connected, MTU 517", true);
    size_t n = from_hex(HW_STATE_SINGLE, pkt, sizeof(pkt));
    nano_decode_state(pkt + 2, n - 2, &st);
    nano_ui_set_state(&st, &meta);
    nano_ui_set_stale(false);
    render(200);
    save(dir, "1-short-name");

    /* 2. 20-character name (preset 1 dump, two packets joined) */
    static uint8_t body[1024];
    size_t a = from_hex(HW_STATE_P1_1, pkt, sizeof(pkt));
    memcpy(body, pkt + 2, a - 2);
    size_t b = from_hex(HW_STATE_P1_2, pkt, sizeof(pkt));
    memcpy(body + a - 2, pkt + 2, b - 2);
    nano_decode_state(body, a + b - 4, &st);
    nano_ui_set_state(&st, &meta);
    render(200);
    save(dir, "2-long-name");

    /* 2a. tap tempo in progress */
    nano_ui_set_tempo(135.0f, true);
    render(200);
    save(dir, "2a-tap-tempo");
    nano_ui_set_tempo(120.0f, false);

    /* 2b. every switch on this preset (worst case for the label column) */
    {
        uint8_t all[4] = { st.active_preset, st.active_preset, st.active_preset, st.active_preset };
        nano_ui_set_footswitches(all);
        render(200);
        save(dir, "2b-four-badges");
    }

    /* 3. capture bypassed, preset 34, medium name */
    n = from_hex(HW_STATE_AFTER_CAPTURE_BYPASS, pkt, sizeof(pkt));
    nano_decode_state(pkt + 2, n - 2, &st);
    nano_ui_set_state(&st, &meta);
    render(200);
    save(dir, "3-capture-bypassed");

    /* 4. link lost */
    nano_ui_set_status("Link lost", false);
    nano_ui_set_stale(true);
    render(200);
    save(dir, "4-link-lost");

    /* 5..7. overlays */
    nano_ui_show(NANO_VIEW_MENU);
    render(200);
    save(dir, "5-menu");
    nano_ui_show(NANO_VIEW_SETTINGS);
    render(200);
    save(dir, "6-settings");
    nano_ui_show(NANO_VIEW_TUNER);
    nano_ui_set_tuner("A#", -7.3f, false);
    render(200);
    save(dir, "7-tuner");
    nano_ui_set_tuner("E", 0.4f, true);
    nano_ui_set_tuner_mute(true);
    render(200);
    save(dir, "8-tuner-in-tune");

    /* 8b. tempo view */
    nano_ui_show(NANO_VIEW_TEMPO);
    nano_ui_set_tempo(120.0f, false);
    render(200);
    save(dir, "8b-tempo");

    /* 9. medium name, 8 per bank */
    nano_ui_show(NANO_VIEW_MAIN);
    nano_ui_set_stale(false);
    nano_ui_set_status("Connected, MTU 517", true);
    nano_ui_set_bank_size(8);
    n = from_hex(HW_STATE_SEG_1, pkt, sizeof(pkt));
    memcpy(body, pkt + 2, n - 2);
    b = from_hex(HW_STATE_SEG_2, pkt, sizeof(pkt));
    memcpy(body + n - 2, pkt + 2, b - 2);
    nano_decode_state(body, n + b - 4, &st);
    nano_ui_set_state(&st, &meta);
    render(200);
    save(dir, "9-medium-name-bank8");
    return 0;
}
