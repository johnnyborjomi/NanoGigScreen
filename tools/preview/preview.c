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

    nano_ui_callbacks_t cb = { .on_prev_preset = noop, .on_next_preset = noop, .on_toggle_fx = noop_fx, .on_toggle_gate = noop_b, .on_tuner = noop_b, .on_tuner_mute = noop_b, .on_tempo_delta = noop_i, .on_tempo_view = noop_b, .on_link = noop_b, .on_bank_size = noop_u8, .on_label_style = noop_u8, .on_outputs_mute = noop_b, .on_expression_show = noop_b, .on_rotation = noop_b, .on_brightness = noop_u8, .on_capture_volume = noop_u8 };
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

    /* 0. boot: the connect page while scanning, then after a deliberate disconnect */
    nano_ui_set_status("Looking for the pedal", false);
    nano_ui_set_stale(true);
    nano_ui_set_connected(false);
    render(200);
    save(dir, "0-boot");
    nano_ui_set_link_enabled(false);
    nano_ui_set_status("Disconnected", false);
    render(200);
    save(dir, "0b-disconnected");
    nano_ui_set_link_enabled(true);

    /* 1. connected, short name (real dump: preset 14) */
    nano_ui_set_status("Connected", true);
    size_t n = from_hex(HW_STATE_SINGLE, pkt, sizeof(pkt));
    nano_decode_state(pkt + 2, n - 2, &st);
    nano_ui_set_state(&st, &meta);
    nano_ui_set_stale(false);
    nano_ui_set_connected(true);
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

    /* 2a. tap tempo in progress, outputs muted */
    nano_ui_set_tempo(135.0f, true);
    nano_ui_set_outputs_muted(true);
    render(200);
    save(dir, "2a-tap-tempo-muted");
    nano_ui_set_tempo(120.0f, false);

    /* 2b. expression pedal at 160/254: post 3 range 17..130 (as captured), pre 1 full range, post 1 heel-toe bypass */
    {
        nano_exp_assignments_t ea;
        memset(&ea, 0, sizeof(ea));
        ea.fx_range[4] = (nano_exp_range_t){ .assigned = true, .min = 17, .max = 130 };
        ea.fx_range[0] = (nano_exp_range_t){ .assigned = true, .min = 0, .max = 255 };
        ea.fx_bypass_mode[2] = 2;
        nano_ui_set_expression_assignments(&ea);
        nano_ui_set_expression(160);
        render(200);
        save(dir, "2b-expression");
        nano_ui_set_expression(-1);
        nano_ui_set_expression_assignments(NULL);
        nano_ui_set_expression_show(true);
    }

    /* 2c. label styles: A2 and numeric */
    nano_ui_set_label_style(NANO_LABEL_LETTER_NUMBER);
    nano_ui_set_state(&st, &meta);
    render(200);
    save(dir, "2c-label-a2");
    nano_ui_set_label_style(NANO_LABEL_NUMERIC);
    nano_ui_set_state(&st, &meta);
    render(200);
    save(dir, "2d-label-numeric");
    nano_ui_set_label_style(NANO_LABEL_NUMBER_LETTER);
    nano_ui_set_outputs_muted(false);

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

    /* 4. link lost: back to the connect page, the menu opens over it */
    nano_ui_set_status("Link lost", false);
    nano_ui_set_stale(true);
    nano_ui_set_connected(false);
    render(200);
    save(dir, "4-link-lost");

    /* 5..7. overlays */
    nano_ui_show(NANO_VIEW_MENU);
    render(200);
    save(dir, "5-menu");
    nano_ui_show(NANO_VIEW_SETTINGS);
    nano_ui_set_outputs_muted(true);
    render(200);
    save(dir, "6-settings");
    nano_ui_settings_page(1);
    nano_ui_set_brightness(7);
    nano_ui_set_rotation(true);
    render(200);
    save(dir, "6b-settings-display");
    nano_ui_set_rotation(false);
    nano_ui_settings_page(0);
    nano_ui_set_outputs_muted(false);
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

    /* 9b. presets list: 4 per bank (opens on the shown preset's bank), 8 per bank, 3 per bank */
    nano_ui_set_preset(14, &meta);
    nano_ui_show(NANO_VIEW_PRESETS);
    render(200);
    save(dir, "9b-presets-4");
    nano_ui_show(NANO_VIEW_MAIN);
    nano_ui_set_bank_size(8);
    nano_ui_set_preset(0, &meta);
    nano_ui_show(NANO_VIEW_PRESETS);
    render(200);
    save(dir, "9c-presets-8");
    nano_ui_show(NANO_VIEW_MAIN);
    nano_ui_set_bank_size(3);
    nano_ui_set_preset(63, &meta);
    nano_ui_show(NANO_VIEW_PRESETS);
    render(200);
    save(dir, "9d-presets-3-last");
    nano_ui_set_bank_size(4);
    nano_ui_set_preset(33, &meta);
    nano_ui_show(NANO_VIEW_MAIN);
    render(200);
    save(dir, "9e-main-list-button");
    {
        /* Names longer than the line: one line, ellipsis. */
        nano_state_t lng = st;
        strcpy(lng.capture_name, "Friedman BE-100 Deluxe HBE Ch2");
        strcpy(lng.ir_short_name, "4x12 Mesa OS V30 SM57 CapEdge");
        nano_ui_set_stale(false);
        nano_ui_set_state(&lng, &meta);
        render(200);
        save(dir, "9f-long-capture-ir");
        /* Four-line tile names drop to the 10 px font. */
        strcpy(lng.fx[0].id, "8C27");
        lng.fx[0].model = nano_lookup_fx_model("8C27");
        strcpy(lng.fx[1].id, "9527");
        lng.fx[1].model = nano_lookup_fx_model("9527");
        lng.fx_on[0] = true;
        nano_ui_set_state(&lng, &meta);
        render(200);
        save(dir, "9g-four-line-tile");
        /* Capture page: 144 = +2.1 dB, 102 = -2.9 dB. */
        lng.capture_volume = 144;
        lng.capture_on = true;
        nano_ui_set_state(&lng, &meta);
        nano_ui_show(NANO_VIEW_CAPTURE);
        render(200);
        save(dir, "9h-capture-page");
        lng.capture_on = false;
        lng.capture_volume = 102;
        strcpy(lng.capture_name, "CA John's Ch1 1");
        nano_ui_set_state(&lng, &meta);
        render(200);
        save(dir, "9i-capture-page-off");
        nano_ui_show(NANO_VIEW_MAIN);
    }

    /* 10. firmware: settings page 3, then the update view in each state */
    nano_ui_set_firmware_version("v0.3.0");
    nano_ui_update_set_wifi("Studio 5G");
    nano_ui_show(NANO_VIEW_SETTINGS);
    nano_ui_settings_page(2);
    render(200);
    save(dir, "10-settings-firmware");
    nano_ui_settings_page(0);
    nano_ui_show(NANO_VIEW_UPDATE);
    nano_ui_update_status(NANO_UPDATE_BUSY, "Connecting to Studio 5G", 0);
    render(200);
    save(dir, "10a-update-connecting");
    nano_ui_update_status(NANO_UPDATE_AVAILABLE, "v0.4.0", 0);
    render(200);
    save(dir, "10b-update-available");
    nano_ui_update_status(NANO_UPDATE_DOWNLOADING, NULL, 42);
    render(200);
    save(dir, "10c-update-downloading");
    nano_ui_update_status(NANO_UPDATE_UP_TO_DATE, "v0.3.0", 0);
    render(200);
    save(dir, "10d-update-up-to-date");
    nano_ui_update_status(NANO_UPDATE_ERROR, "Wrong Wi-Fi password?", 0);
    render(200);
    save(dir, "10e-update-error");
    {
        nano_ui_network_t nets[] = {
            { "Studio 5G", -48, true }, { "Venue Guest", -61, false }, { "Neighbours Wi-Fi With A Long Name", -70, true },
            { "iPhone", -79, true }, { "PrinterSetup", -86, false },
        };
        nano_ui_update_show_networks(nets, 5, false);
        render(200);
        save(dir, "10f-update-networks");
        /* tap the first (secured) network: the password page with its keyboard */
        lv_obj_t *update = lv_obj_get_child(lv_screen_active(), -1);
        lv_obj_t *list = lv_obj_get_child(lv_obj_get_child(update, -2), -1);
        lv_obj_send_event(lv_obj_get_child(list, 0), LV_EVENT_CLICKED, NULL);
        render(200);
        save(dir, "10g-update-password");
    }

    /* 9. medium name, 8 per bank */
    nano_ui_show(NANO_VIEW_MAIN);
    nano_ui_set_stale(false);
    nano_ui_set_status("Connected", true);
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
