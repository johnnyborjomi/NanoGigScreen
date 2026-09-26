/* Host tests for nano_protocol; build with the CMakeLists.txt next to this file. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fixtures.h"
#include "nano_assembler.h"
#include "nano_decode.h"
#include "nano_frame.h"
#include "nano_models.h"
#include "nano_proto.h"

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)
#define CHECK_STR(a, b) do { if (strcmp((a), (b)) != 0) { printf("  FAIL %s:%d: \"%s\" != \"%s\"\n", __FILE__, __LINE__, (a), (b)); failures++; } } while (0)

static size_t from_hex(const char *text, uint8_t *out, size_t cap)
{
    size_t n = 0;
    int hi = -1;
    for (const char *p = text; *p; p++) {
        int v;
        if (*p >= '0' && *p <= '9') v = *p - '0';
        else if (*p >= 'A' && *p <= 'F') v = *p - 'A' + 10;
        else if (*p >= 'a' && *p <= 'f') v = *p - 'a' + 10;
        else continue;
        if (hi < 0) { hi = v; continue; }
        if (n >= cap) return 0;
        out[n++] = (uint8_t)(hi * 16 + v);
        hi = -1;
    }
    return n;
}

/* ---- assembler harness -------------------------------------------------- */

static uint8_t got[4096];
static size_t got_len;
static int got_packets;
static bool got_complete;
static int got_count;

static void on_message(void *ctx, const uint8_t *body, size_t len, int packets, bool complete)
{
    (void)ctx;
    memcpy(got, body, len);
    got_len = len;
    got_packets = packets;
    got_complete = complete;
    got_count++;
}

static void test_varint(void)
{
    printf("varint\n");
    uint64_t v;
    const uint8_t one[] = { 0x9A, 0x01 };
    CHECK(nano_read_varint(one, 2, &v) == 2 && v == 154);
    const uint8_t minus_one[] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x01 };
    CHECK(nano_read_varint(minus_one, 10, &v) == 10 && v == UINT64_MAX);
    const uint8_t trunc[] = { 0x80 };
    CHECK(nano_read_varint(trunc, 1, &v) == 0);
    uint8_t buf[10];
    CHECK(nano_write_varint(buf, 10, 300) == 2 && buf[0] == 0xAC && buf[1] == 0x02);
}

static void test_frame_header(void)
{
    printf("frame header\n");
    nano_frame_header_t h;
    uint8_t pkt[512];
    size_t n = from_hex(HW_STATE_SINGLE, pkt, sizeof(pkt));
    CHECK(n == 511);
    CHECK(nano_parse_frame_header(pkt, n, &h) && h.start && h.end && h.body_length == 509);
    n = from_hex(HW_STATE_SEG_1, pkt, sizeof(pkt));
    CHECK(n == 512);
    CHECK(nano_parse_frame_header(pkt, n, &h) && h.start && !h.end && h.body_length == 510);
    n = from_hex(HW_STATE_SEG_2, pkt, sizeof(pkt));
    CHECK(nano_parse_frame_header(pkt, n, &h) && !h.start && h.end && h.body_length == 238);
    CHECK(!nano_parse_frame_header(pkt, n - 1, &h)); /* length mismatch */
    int t;
    const uint8_t ev[] = { 0x08, 0x01, 0x1F, 0x00, 0x00, 0x00 };
    CHECK(nano_split_trailer(ev, 6, &t) == 2 && t == 0x1F);
    uint8_t pitch[32];
    n = from_hex(HW_TUNER_PITCH_IN_TUNE, pitch, sizeof(pitch));
    CHECK(nano_is_tuner_pitch_packet(pitch, n));
    CHECK(!nano_is_tuner_pitch_packet(pkt, 240));
}

static void test_request_frames(void)
{
    printf("request frames\n");
    uint8_t buf[64], want[64];
    size_t n = nano_build_preset_select(buf, sizeof(buf), 5);
    size_t w = from_hex("36 C0 18 00 20 05 28 FF FF FF FF FF FF FF FF FF 01 30 FF FF FF FF FF FF FF FF FF 01 38 FF FF FF FF FF FF FF FF FF 01 40 FF FF FF FF FF FF FF FF FF 01 48 04 1D 00 00 00", want, sizeof(want));
    CHECK(n == 56 && w == 56 && memcmp(buf, want, 56) == 0);
    CHECK(buf[0] == n - 2); /* length byte convention */
    CHECK(nano_build_preset_select(buf, sizeof(buf), 64) == 0);
    n = nano_build_fx_bypass(buf, sizeof(buf), 0, false);
    w = from_hex("0A C0 08 01 18 04 20 01 1F 00 00 00", want, sizeof(want));
    CHECK(n == 12 && memcmp(buf, want, 12) == 0);
    n = nano_build_gate_bypass(buf, sizeof(buf), true);
    w = from_hex("0A C0 08 01 18 09 20 00 1F 00 00 00", want, sizeof(want));
    CHECK(n == 12 && memcmp(buf, want, 12) == 0);
    n = nano_build_tuner_on(buf, sizeof(buf), 440.0f, false);
    w = from_hex("0F C0 20 01 2D 00 00 DC 43 30 01 38 00 7F 00 00 00", want, sizeof(want));
    CHECK(n == 17 && memcmp(buf, want, 17) == 0);
    w = from_hex("0C C0 08 03 18 01 20 01 28 01 01 00 00 00", want, sizeof(want));
    CHECK(memcmp(NANO_REQ_STATE, want, 14) == 0);
}

static void test_state_single(void)
{
    printf("state dump: single packet\n");
    uint8_t pkt[512];
    size_t n = from_hex(HW_STATE_SINGLE, pkt, sizeof(pkt));
    nano_state_t s;
    CHECK(nano_decode_state(pkt + 2, n - 2, &s));
    CHECK(s.active_preset == 14);
    CHECK(s.has_bypass);
    CHECK(s.fx_on[0] && !s.fx_on[1] && s.fx_on[2] && s.fx_on[3] && s.fx_on[4]);
    CHECK(s.cab_on);
    CHECK(s.gate_on);
    CHECK(s.capture_on);
    CHECK(s.amp[0] == 154);
    CHECK_STR(s.capture_name, "CA John's Ch1 1");
    CHECK_STR(s.ir_short_name, "110 US PRN C10R");
    CHECK_STR(s.firmware, "2.2.1");
    CHECK(s.tempo_bpm == 120.0f);
    CHECK(s.footswitch[0] == 3 && s.footswitch[1] == 5 && s.footswitch[2] == 20 && s.footswitch[3] == 14); /* 70 03, 78 05, B0 02 14, B8 02 0E */
    CHECK_STR(s.fx[0].id, "17");
    CHECK(s.fx[0].model && strcmp(s.fx[0].model->name, "Exotic Z Boost") == 0 && s.fx[0].model->category == NANO_CAT_OVERDRIVE);
    CHECK(s.fx[1].model && strcmp(s.fx[1].model->name, "Legendary 87 (M)") == 0);
    CHECK(s.fx[2].model && strcmp(s.fx[2].model->name, "Doubler") == 0);
    CHECK(s.fx[3].model && strcmp(s.fx[3].model->name, "Analog Delay") == 0 && s.fx[3].model->category == NANO_CAT_DELAY);
    CHECK(s.fx[4].model && strcmp(s.fx[4].model->name, "Mind Hall") == 0 && s.fx[4].model->category == NANO_CAT_REVERB);
}

static void test_state_segmented(void)
{
    printf("state dump: two packets through the assembler\n");
    static uint8_t buf[2048];
    nano_assembler_t a;
    nano_assembler_init(&a, buf, sizeof(buf), on_message, NULL);
    uint8_t pkt[512];
    got_count = 0;
    size_t n = from_hex(HW_STATE_SEG_1, pkt, sizeof(pkt));
    nano_assembler_push(&a, pkt, n, 1000);
    CHECK(got_count == 0 && nano_assembler_open(&a));
    n = from_hex(HW_STATE_SEG_2, pkt, sizeof(pkt));
    nano_assembler_push(&a, pkt, n, 1010);
    CHECK(got_count == 1 && got_complete && got_packets == 2 && got_len == 510 + 238);
    CHECK(!nano_assembler_open(&a));
    nano_state_t s;
    CHECK(nano_decode_state(got, got_len, &s));
    CHECK(s.active_preset == 3);
    CHECK(!s.fx_on[0] && s.fx_on[1] && s.fx_on[2] && !s.fx_on[3] && !s.fx_on[4]);
    CHECK(s.amp[0] == 159);
    CHECK_STR(s.capture_name, "EVH 5150III Ch3 Gain3");
    CHECK_STR(s.ir_short_name, "412 CA Stand OS A V30 '01");
    CHECK(s.fx[0].model && strcmp(s.fx[0].model->name, "Transpose") == 0 && s.fx[0].model->category == NANO_CAT_PITCH);
    CHECK(s.fx[1].model && strcmp(s.fx[1].model->name, "Green 808") == 0);
    CHECK(s.fx[4].model && strcmp(s.fx[4].model->name, "Ambience") == 0);

    /* Single-packet events pass straight through. */
    got_count = 0;
    n = from_hex(HW_BYPASS_CHANGED, pkt, sizeof(pkt));
    nano_assembler_push(&a, pkt, n, 2000);
    CHECK(got_count == 1 && got_complete && got_packets == 1 && got_len == 6);

    /* Inactivity flush of a stalled fragment. */
    got_count = 0;
    n = from_hex(HW_STATE_SEG_1, pkt, sizeof(pkt));
    nano_assembler_push(&a, pkt, n, 3000);
    nano_assembler_tick(&a, 3000 + NANO_ASSEMBLER_INACTIVITY_MS - 1);
    CHECK(got_count == 0);
    nano_assembler_tick(&a, 3000 + NANO_ASSEMBLER_INACTIVITY_MS);
    CHECK(got_count == 1 && !got_complete);

    /* A new START while open flushes the partial one first. */
    got_count = 0;
    nano_assembler_push(&a, pkt, n, 4000);
    nano_assembler_push(&a, pkt, n, 4001);
    CHECK(got_count == 1 && !got_complete && nano_assembler_open(&a));
    nano_assembler_reset(&a);

    /* Overflow: a dump larger than the buffer is dropped, not truncated. */
    static uint8_t small[600];
    nano_assembler_init(&a, small, sizeof(small), on_message, NULL);
    got_count = 0;
    nano_assembler_push(&a, pkt, n, 5000);
    n = from_hex(HW_STATE_SEG_2, pkt, sizeof(pkt));
    nano_assembler_push(&a, pkt, n, 5001);
    CHECK(got_count == 0 && !nano_assembler_open(&a));
}

static void test_state_preset_one_and_bypassed_capture(void)
{
    printf("state dump: preset 1 has no field 13; bypassed capture\n");
    static uint8_t buf[2048];
    nano_assembler_t a;
    nano_assembler_init(&a, buf, sizeof(buf), on_message, NULL);
    uint8_t pkt[512];
    got_count = 0;
    size_t n = from_hex(HW_STATE_P1_1, pkt, sizeof(pkt));
    nano_assembler_push(&a, pkt, n, 0);
    n = from_hex(HW_STATE_P1_2, pkt, sizeof(pkt));
    nano_assembler_push(&a, pkt, n, 1);
    CHECK(got_count == 1 && got_complete);
    nano_state_t s;
    CHECK(nano_decode_state(got, got_len, &s));
    CHECK(s.active_preset == 0);
    CHECK_STR(s.capture_name, "Stealth EL34 Gojira Blue");
    CHECK(s.capture_on);

    n = from_hex(HW_STATE_AFTER_CAPTURE_BYPASS, pkt, sizeof(pkt));
    CHECK(nano_decode_state(pkt + 2, n - 2, &s));
    CHECK(s.active_preset == 33);
    CHECK(!s.capture_on);
    CHECK(!s.cab_on);
    CHECK_STR(s.capture_name, "US Prince 65 4");
}

static void test_events(void)
{
    printf("events\n");
    uint8_t pkt[64];
    nano_event_t e;
    size_t n = from_hex(HW_PRESET_CHANGED, pkt, sizeof(pkt));
    nano_decode_event(pkt, n, &e);
    CHECK(e.kind == NANO_EV_PRESET_CHANGED && e.preset == 3);
    CHECK(e.footswitch[0] == 3 && e.footswitch[1] == 5 && e.footswitch[2] == 20 && e.footswitch[3] == 14);
    /* Preset 1: field 4 absent. */
    n = from_hex("0E C0 08 01 28 03 30 05 38 14 40 0E 1D 00 00 00", pkt, sizeof(pkt));
    nano_decode_event(pkt, n, &e);
    CHECK(e.kind == NANO_EV_PRESET_CHANGED && e.preset == 0);
    n = from_hex(HW_BYPASS_CHANGED, pkt, sizeof(pkt));
    nano_decode_event(pkt, n, &e);
    CHECK(e.kind == NANO_EV_BYPASS_CHANGED);
    n = from_hex(HW_UNKNOWN_73, pkt, sizeof(pkt));
    nano_decode_event(pkt, n, &e);
    CHECK(e.kind == NANO_EV_CONTROL && e.msg_type == 0x73);
    n = from_hex(HW_PRESET_SELECT_ACK, pkt, sizeof(pkt));
    nano_decode_event(pkt, n, &e);
    CHECK(e.kind == NANO_EV_PRESET_SELECT_ACK);
    n = from_hex(HW_TUNER_PITCH_IN_TUNE, pkt, sizeof(pkt));
    nano_decode_event(pkt, n, &e);
    CHECK(e.kind == NANO_EV_TUNER_PITCH && e.in_tune && e.cents == 0.0f);
    CHECK_STR(e.note, "A");
    n = from_hex("0B C0 08 01 18 02 20 FE 01 40 00 00 00", pkt, sizeof(pkt)); /* toe: 254 as a 2-byte varint */
    nano_decode_event(pkt, n, &e);
    CHECK(e.kind == NANO_EV_EXPRESSION && e.position == 254);
    n = from_hex("0D C0 08 01 20 01 2D 00 00 DC 43 7F 00 00 00", pkt, sizeof(pkt));
    nano_decode_event(pkt, n, &e);
    CHECK(e.kind == NANO_EV_TUNER_ACK && e.tuner_on && e.reference_hz == 440.0f);
    /* Garbage does not fault. */
    n = from_hex("FF FF FF", pkt, sizeof(pkt));
    nano_decode_event(pkt, n, &e);
    CHECK(e.kind == NANO_EV_UNKNOWN);
}

/* Build a metadata message from scratch: 3 presets, 2 captures, 1 IR, with a 4-byte trailer. */
static size_t build_metadata(uint8_t *out, size_t cap)
{
    (void)cap;
    size_t n = 0;
    out[n++] = 0x08; out[n++] = 0x01; /* field 1 = 1 */
    out[n++] = 0x68; out[n++] = 0x02; /* field 13 = 2 (stale state: ignored) */
    const char *names[3] = { "Clean", "Crunch", "Lead" };
    const char *caps[3] = { "CA John's Ch1 1", "5150 Red", "" };
    for (int i = 0; i < 3; i++) {
        uint8_t rec[96];
        size_t r = 0;
        rec[r++] = 0x0A; rec[r++] = (uint8_t)strlen(names[i]); memcpy(rec + r, names[i], strlen(names[i])); r += strlen(names[i]);
        rec[r++] = 0x3A; rec[r++] = (uint8_t)strlen(caps[i]); memcpy(rec + r, caps[i], strlen(caps[i])); r += strlen(caps[i]);
        rec[r++] = 0x42; rec[r++] = 12; memcpy(rec + r, "0123456789ab", 12); r += 12; /* capture id: filtered */
        rec[r++] = 0x4A; rec[r++] = 3; memcpy(rec + r, "IR1", 3); r += 3;
        out[n++] = 0x92; out[n++] = 0x01; out[n++] = (uint8_t)r; memcpy(out + n, rec, r); n += r;
    }
    for (int i = 0; i < 2; i++) {
        const char *nm = i ? "Marshall Plexi" : "Fender Twin";
        uint8_t rec[64];
        size_t r = 0;
        rec[r++] = 0x0A; rec[r++] = 4; memcpy(rec + r, "abcd", 4); r += 4;
        rec[r++] = 0x12; rec[r++] = (uint8_t)strlen(nm); memcpy(rec + r, nm, strlen(nm)); r += strlen(nm);
        out[n++] = 0x8A; out[n++] = 0x01; out[n++] = (uint8_t)r; memcpy(out + n, rec, r); n += r;
    }
    out[n++] = 0x9A; out[n++] = 0x01; out[n++] = 5; out[n++] = 0x0A; out[n++] = 3; memcpy(out + n, "IR1", 3); n += 3;
    out[n++] = 0x02; out[n++] = 0; out[n++] = 0; out[n++] = 0; /* trailer */
    return n;
}

static void test_metadata(void)
{
    printf("metadata\n");
    static uint8_t msg[512];
    size_t n = build_metadata(msg, sizeof(msg));
    static nano_metadata_t m;
    CHECK(nano_decode_metadata(msg, n, &m));
    CHECK(m.preset_record_count == 3);
    CHECK_STR(m.presets[0].name, "Clean");
    CHECK_STR(m.presets[0].capture_name, "CA John's Ch1 1");
    CHECK_STR(m.presets[0].ir_short_name, "IR1");
    CHECK_STR(m.presets[2].name, "Lead");
    CHECK_STR(m.presets[2].capture_name, "");
    CHECK_STR(m.presets[3].name, ""); /* padded */
    CHECK(m.capture_count == 2);
    CHECK_STR(m.captures[1], "Marshall Plexi");
    CHECK(m.ir_count == 1);
    CHECK_STR(m.irs[0], "IR1");
    /* A partial prefix is scanned past. */
    static uint8_t shifted[520];
    shifted[0] = 0xFF; shifted[1] = 0x00;
    memcpy(shifted + 2, msg, n);
    CHECK(nano_decode_metadata(shifted, n + 2, &m) && m.preset_record_count == 3);
    /* A state dump has no preset records. */
    uint8_t pkt[512];
    n = from_hex(HW_STATE_SINGLE, pkt, sizeof(pkt));
    CHECK(!nano_decode_metadata(pkt + 2, n - 2, &m));
}

static void test_labels_and_models(void)
{
    printf("labels and models\n");
    char l[8];
    nano_preset_label(0, 4, l, sizeof(l)); CHECK_STR(l, "1A");
    nano_preset_label(9, 4, l, sizeof(l)); CHECK_STR(l, "3B");
    nano_preset_label(63, 4, l, sizeof(l)); CHECK_STR(l, "16D");
    nano_preset_label(9, 8, l, sizeof(l)); CHECK_STR(l, "2B");
    CHECK(nano_lookup_fx_model("A51F") && nano_lookup_fx_model("A51F")->category == NANO_CAT_UTILITY_EQ);
    CHECK(nano_lookup_fx_model("ZZ") == NULL && nano_lookup_fx_model("") == NULL);
    CHECK(nano_category_color(NANO_CAT_DELAY) == 0x00F0D8);
    CHECK(nano_category_light_text(NANO_CAT_MODULATION) && !nano_category_light_text(NANO_CAT_DELAY));
}

int main(void)
{
    test_varint();
    test_frame_header();
    test_request_frames();
    test_state_single();
    test_state_segmented();
    test_state_preset_one_and_bypassed_capture();
    test_events();
    test_metadata();
    test_labels_and_models();
    if (failures) {
        printf("%d failure(s)\n", failures);
        return 1;
    }
    printf("all protocol tests passed\n");
    return 0;
}
