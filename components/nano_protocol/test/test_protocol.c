/* Host tests for nano_protocol; build with the CMakeLists.txt next to this file. */
#include <math.h>
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
    n = nano_build_tempo_set(buf, sizeof(buf), 99.0f);
    w = from_hex("0D C0 08 01 18 01 2D 00 00 C6 42 91 00 00 00", want, sizeof(want));
    CHECK(n == 15 && memcmp(buf, want, 15) == 0);
    n = nano_build_tempo_exit(buf, sizeof(buf), 99.0f);
    w = from_hex("0B C0 08 01 2D 00 00 C6 42 91 00 00 00", want, sizeof(want));
    CHECK(n == 13 && memcmp(buf, want, 13) == 0);
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
    CHECK(s.capture_volume == 144); /* field 44 */
    CHECK_STR(s.capture_name, "CA John's Ch1 1");
    CHECK_STR(s.ir_short_name, "110 US PRN C10R");
    CHECK_STR(s.firmware, "2.2.1");
    CHECK(s.tempo_bpm == 120.0f);
    CHECK(s.tuner_reference_hz == 440.0f); /* F5 02 00 00 DC 43 */
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
    CHECK(e.kind == NANO_EV_TUNER_ACK && e.tuner_on && e.reference_hz == 440.0f && !e.tuner_muted);
    n = from_hex("0F C0 08 01 20 01 2D 00 00 DC 43 38 01 7F 00 00 00", pkt, sizeof(pkt));
    nano_decode_event(pkt, n, &e);
    CHECK(e.kind == NANO_EV_TUNER_ACK && e.tuner_on && e.tuner_muted);
    n = from_hex("0B C0 08 01 2D 00 00 DC 43 7F 00 00 00", pkt, sizeof(pkt)); /* pedal ended its tuner */
    nano_decode_event(pkt, n, &e);
    CHECK(e.kind == NANO_EV_TUNER_ACK && !e.tuner_on);
    /* Tap tempo (2026-09-26): a tap at 135 BPM, then leaving the mode at 99 BPM. */
    n = from_hex("0D C0 08 01 18 01 2D 00 00 07 43 91 00 00 00", pkt, sizeof(pkt));
    nano_decode_event(pkt, n, &e);
    CHECK(e.kind == NANO_EV_TAP_TEMPO && e.tap_active && e.tempo_bpm == 135.0f);
    n = from_hex("0B C0 08 01 2D 00 00 C6 42 91 00 00 00", pkt, sizeof(pkt));
    nano_decode_event(pkt, n, &e);
    CHECK(e.kind == NANO_EV_TAP_TEMPO && !e.tap_active && e.tempo_bpm == 99.0f);
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
    /* Pad to a realistic size with an unknown bytes field (field 100), as the real dump is ~17 KB. */
    size_t pad = 2100;
    out[n++] = 0xA2; out[n++] = 0x06; /* field 100, wire 2 */
    out[n++] = (uint8_t)(0x80 | (pad & 0x7f)); out[n++] = (uint8_t)(pad >> 7);
    memset(out + n, 'x', pad); n += pad;
    out[n++] = 0x02; out[n++] = 0; out[n++] = 0; out[n++] = 0; /* trailer */
    return n;
}

static void test_metadata(void)
{
    printf("metadata\n");
    static uint8_t msg[4096];
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
    static uint8_t shifted[4100];
    shifted[0] = 0xFF; shifted[1] = 0x00;
    memcpy(shifted + 2, msg, n);
    CHECK(nano_decode_metadata(shifted, n + 2, &m) && m.preset_record_count == 3);
    /* A state dump is never metadata, even when a shifted parse yields one bogus record. */
    uint8_t pkt[512];
    n = from_hex(HW_STATE_SINGLE, pkt, sizeof(pkt));
    CHECK(!nano_decode_metadata(pkt + 2, n - 2, &m));
    /* A large message with a single record is not metadata either. */
    static uint8_t one[4096];
    size_t k = 0;
    one[k++] = 0x92; one[k++] = 0x01; one[k++] = 4; one[k++] = 0x0A; one[k++] = 2; one[k++] = 'A'; one[k++] = 'b';
    one[k++] = 0xA2; one[k++] = 0x06; one[k++] = 0x90; one[k++] = 0x10; memset(one + k, 'x', 2064); k += 2064;
    CHECK(!nano_decode_metadata(one, k, &m));
}

static void test_labels_and_models(void)
{
    printf("labels and models\n");
    char l[8];
    nano_preset_label(0, 4, NANO_LABEL_NUMBER_LETTER, l, sizeof(l)); CHECK_STR(l, "1A");
    nano_preset_label(9, 4, NANO_LABEL_NUMBER_LETTER, l, sizeof(l)); CHECK_STR(l, "3B");
    nano_preset_label(63, 4, NANO_LABEL_NUMBER_LETTER, l, sizeof(l)); CHECK_STR(l, "16D");
    nano_preset_label(9, 8, NANO_LABEL_NUMBER_LETTER, l, sizeof(l)); CHECK_STR(l, "2B");
    nano_preset_label(9, 8, NANO_LABEL_LETTER_NUMBER, l, sizeof(l)); CHECK_STR(l, "B2");
    nano_preset_label(9, 4, NANO_LABEL_LETTER_NUMBER, l, sizeof(l)); CHECK_STR(l, "C2");
    nano_preset_label(63, 2, NANO_LABEL_LETTER_NUMBER, l, sizeof(l)); CHECK_STR(l, "AF2"); /* bank 31 */
    nano_preset_label(9, 4, NANO_LABEL_NUMERIC, l, sizeof(l)); CHECK_STR(l, "10");
    nano_preset_label(63, 4, NANO_LABEL_NUMERIC, l, sizeof(l)); CHECK_STR(l, "64");
    /* Outputs mute write (byte-exact from Cortex Cloud) and the pedal's replies. */
    uint8_t m[10];
    CHECK(nano_build_outputs_mute(m, sizeof(m), true) == 10 && m[4] == 0x68 && m[5] == 1 && m[6] == 0x43);
    CHECK(nano_build_outputs_mute(m, sizeof(m), false) == 10 && m[5] == 0);

    printf("preset rename\n");
    {
        /* Byte-exact against the frames the pedal accepted (probe 2026-10-08, preset 45 "Gojira T"). */
        const uint8_t gt[] = { 0x12, 0xC0, 0x08, 0x01, 0x18, 0x2C, 0x22, 0x08, 'G', 'o', 'j', 'i', 'r', 'a', ' ', 'T', 0x6F, 0x00, 0x00, 0x00 };
        uint8_t f[40];
        CHECK(nano_build_preset_rename(f, sizeof(f), 44, "Gojira T") == sizeof(gt) && memcmp(f, gt, sizeof(gt)) == 0);
        CHECK(nano_build_preset_rename(f, sizeof(f), 44, "") == 0);
        CHECK(nano_build_preset_rename(f, sizeof(f), 64, "Name") == 0);
        uint8_t big[64];
        CHECK(nano_build_preset_rename(big, sizeof(big), 0, "12345678901234567890123456789012") == 0); /* 32 > NANO_PRESET_NAME_MAX */
        CHECK(nano_build_preset_rename(big, sizeof(big), 0, "1234567890123456789012345678901") == 43);
        CHECK(nano_build_preset_rename(f, 19, 44, "Gojira T") == 0);
        /* The pedal's reply. */
        const uint8_t ok[] = { 0x0A, 0xC0, 0x08, 0x06, 0x18, 0x2C, 0x20, 0x01, 0x70, 0x00, 0x00, 0x00 };
        nano_event_t ev;
        nano_decode_event(ok, sizeof(ok), &ev);
        CHECK(ev.kind == NANO_EV_RENAME_REPLY && ev.preset == 44 && ev.ok);
        const uint8_t no[] = { 0x08, 0xC0, 0x08, 0x06, 0x18, 0x2C, 0x70, 0x00, 0x00, 0x00 };
        nano_decode_event(no, sizeof(no), &ev);
        CHECK(ev.kind == NANO_EV_RENAME_REPLY && ev.preset == 44 && !ev.ok);
    }

    printf("capture volume\n");
    {
        /* Byte-exact against Cortex Cloud's writes (HCI snoop 2026-10-07). */
        const uint8_t v0[] = { 0x0A, 0xC0, 0x18, 0x0A, 0x20, 0x00, 0x28, 0x00, 0x1A, 0x00, 0x00, 0x00 };
        const uint8_t v102[] = { 0x0A, 0xC0, 0x18, 0x0A, 0x20, 0x66, 0x28, 0x00, 0x1A, 0x00, 0x00, 0x00 };
        const uint8_t v128[] = { 0x0B, 0xC0, 0x18, 0x0A, 0x20, 0x80, 0x01, 0x28, 0x00, 0x1A, 0x00, 0x00, 0x00 };
        const uint8_t v255[] = { 0x0B, 0xC0, 0x18, 0x0A, 0x20, 0xFF, 0x01, 0x28, 0x00, 0x1A, 0x00, 0x00, 0x00 };
        uint8_t f[16];
        CHECK(nano_build_capture_volume(f, sizeof(f), 0) == 12 && memcmp(f, v0, 12) == 0);
        CHECK(nano_build_capture_volume(f, sizeof(f), 102) == 12 && memcmp(f, v102, 12) == 0);
        CHECK(nano_build_capture_volume(f, sizeof(f), 128) == 13 && memcmp(f, v128, 13) == 0);
        CHECK(nano_build_capture_volume(f, sizeof(f), 255) == 13 && memcmp(f, v255, 13) == 0);
        CHECK(nano_build_capture_volume(f, 12, 200) == 0);
        /* The scale against Cortex Cloud's readings. */
        CHECK(nano_capture_volume_raw(-24.0f) == 0 && nano_capture_volume_raw(12.0f) == 255);
        CHECK(nano_capture_volume_raw(0.0f) == 128);
        CHECK(nano_capture_volume_raw(-12.0f) == 39);
        CHECK(nano_capture_volume_raw(-2.9f) == 102);
        CHECK(fabsf(nano_capture_volume_db(128) - 0.0f) < 0.05f);
        CHECK(fabsf(nano_capture_volume_db(39) + 12.0f) < 0.05f);
        CHECK(fabsf(nano_capture_volume_db(102) + 2.9f) < 0.05f);
        CHECK(nano_capture_volume_db(0) == -24.0f && fabsf(nano_capture_volume_db(255) - 12.0f) < 0.001f);
        /* Cortex Cloud's readout: 101 shows -3.0 (seen on the pedal 2026-10-07), 102 -2.9, 128 0.0, 39 -12.0. */
        CHECK(nano_capture_volume_tenths(101) == -30 && nano_capture_volume_tenths(102) == -29);
        CHECK(nano_capture_volume_tenths(128) == 0 && nano_capture_volume_tenths(39) == -120);
        CHECK(nano_capture_volume_tenths(0) == -240 && nano_capture_volume_tenths(255) == 120);
        CHECK(nano_capture_volume_tenths(110) == -20); /* -1.995 dB: on the boundary, within the fit */
    }
    nano_event_t ev;
    const uint8_t ack[] = { 0x08, 0xC0, 0x08, 0x01, 0x18, 0x01, 0x44, 0x00, 0x00, 0x00 };
    nano_decode_event(ack, sizeof(ack), &ev); CHECK(ev.kind == NANO_EV_OUTPUTS_MUTE_ACK);
    /* Settings reply, minimal shape: `C0 08 01 ... 80 01 01 42 00 00 00` with field 16 = 1. */
    const uint8_t muted[] = { 0x09, 0xC0, 0x08, 0x01, 0x80, 0x01, 0x01, 0x42, 0x00, 0x00, 0x00 };
    nano_decode_event(muted, sizeof(muted), &ev); CHECK(ev.kind == NANO_EV_SETTINGS && ev.outputs_muted);
    const uint8_t on[] = { 0x06, 0xC0, 0x08, 0x01, 0x42, 0x00, 0x00, 0x00 };
    nano_decode_event(on, sizeof(on), &ev); CHECK(ev.kind == NANO_EV_SETTINGS && !ev.outputs_muted);
    CHECK(nano_lookup_fx_model("A51F") && nano_lookup_fx_model("A51F")->category == NANO_CAT_EQ);
    /* Neural's device list / Cortex Cloud 2026-10-01: ST compressors are compressors, wah and filter differ. */
    CHECK(nano_lookup_fx_model("9427") && nano_lookup_fx_model("9427")->category == NANO_CAT_COMPRESSOR);
    CHECK(nano_lookup_fx_model("8B7D") && nano_lookup_fx_model("8B7D")->category == NANO_CAT_UTILITY);
    CHECK(nano_lookup_fx_model("B646") && nano_lookup_fx_model("B646")->category == NANO_CAT_WAH);
    CHECK(nano_lookup_fx_model("C6BB01") && nano_lookup_fx_model("C6BB01")->category == NANO_CAT_FILTER);
    CHECK(nano_category_color(NANO_CAT_WAH) != nano_category_color(NANO_CAT_UTILITY));
    CHECK(nano_category_light_text(NANO_CAT_EQ));
    CHECK(strcmp(nano_category_short(NANO_CAT_COMPRESSOR), "CMP") == 0 && strcmp(nano_category_short(NANO_CAT_EQ), "EQ") == 0);
    CHECK(strcmp(nano_category_short(NANO_CAT_UNKNOWN), "") == 0 && strcmp(nano_category_short((nano_category_t)99), "") == 0);
    for (int c = 1; c < NANO_CAT_COUNT; c++) CHECK(strlen(nano_category_short((nano_category_t)c)) >= 2);
    CHECK(nano_lookup_fx_model("ZZ") == NULL && nano_lookup_fx_model("") == NULL);
    CHECK(nano_category_color(NANO_CAT_DELAY) == 0x00F0D8);
    CHECK(nano_category_light_text(NANO_CAT_MODULATION) && !nano_category_light_text(NANO_CAT_DELAY));
}

/* Expression pedal (Cortex Cloud captures of 2026-09-19, NanoGig `src/fixtures/hardware-2026-09-19.ts`). */
static void test_expression(void)
{
    printf("expression\n");
    uint8_t pkt[80];
    nano_event_t e;
    /* Values: post 3 = 78 alone; post 2 and post 3 = 224; nothing assigned. */
    size_t n = from_hex("08 C0 08 01 68 4E AA 00 00 00", pkt, sizeof(pkt));
    nano_decode_event(pkt, n, &e);
    CHECK(e.kind == NANO_EV_EXP_VALUES && e.exp_values.fx_value[4] == 78 && e.exp_values.fx_value[3] == -1 && e.exp_values.fx_bypass[4] == -1);
    n = from_hex("0C C0 08 01 60 E0 01 68 E0 01 AA 00 00 00", pkt, sizeof(pkt));
    nano_decode_event(pkt, n, &e);
    CHECK(e.kind == NANO_EV_EXP_VALUES && e.exp_values.fx_value[3] == 224 && e.exp_values.fx_value[4] == 224 && e.exp_values.fx_value[0] == -1);
    n = from_hex("06 C0 08 01 AA 00 00 00", pkt, sizeof(pkt));
    nano_decode_event(pkt, n, &e);
    CHECK(e.kind == NANO_EV_EXP_VALUES);
    for (int i = 0; i < NANO_FX_SLOT_COUNT; i++) CHECK(e.exp_values.fx_value[i] == -1 && e.exp_values.fx_bypass[i] == -1);
    /* Everything assigned, pedal at 96: FX amounts 9..13 = 96, the heel-toe bypass flags 17..21 = 1. */
    n = from_hex("2E C0 08 01 20 60 28 60 30 60 38 60 40 60 48 60 50 60 58 60 60 60 68 60 70 60 88 01 01 90 01 01 98 01 01 A0 01 01 A8 01 01 B0 01 01 AA 00 00 00", pkt, sizeof(pkt));
    nano_decode_event(pkt, n, &e);
    CHECK(e.kind == NANO_EV_EXP_VALUES);
    for (int i = 0; i < NANO_FX_SLOT_COUNT; i++) CHECK(e.exp_values.fx_value[i] == 96 && e.exp_values.fx_bypass[i] == 1);
    /* Assignment replies: post 3 at 17..130 (preset 58) and 15..127 (preset 2). */
    n = from_hex("0D C0 08 01 5A 05 10 11 18 82 01 3D 00 00 00", pkt, sizeof(pkt));
    nano_decode_event(pkt, n, &e);
    CHECK(e.kind == NANO_EV_EXP_ASSIGNMENTS && e.exp_assign.fx_range[4].assigned && e.exp_assign.fx_range[4].min == 17 && e.exp_assign.fx_range[4].max == 130);
    CHECK(!e.exp_assign.fx_range[3].assigned && e.exp_assign.fx_bypass_mode[4] == 0 && !e.exp_assign.capture_bypass && e.exp_assign.amp_ranges == 0);
    n = from_hex("0C C0 08 01 5A 04 10 0F 18 7F 3D 00 00 00", pkt, sizeof(pkt));
    nano_decode_event(pkt, n, &e);
    CHECK(e.kind == NANO_EV_EXP_ASSIGNMENTS && e.exp_assign.fx_range[4].min == 15 && e.exp_assign.fx_range[4].max == 127);
    /* Synthetic (write numbering - 1): pre 1 range 0..255 at 7, post 3 heel-toe bypass at 19, capture bypass at 13, gain at 3. */
    n = from_hex("29 C0 08 01 1A 07 08 00 10 00 18 FF 01 3A 07 08 00 10 00 18 FF 01 6A 06 12 04 08 00 10 00 9A 01 06 12 04 08 00 10 00 3D 00 00 00", pkt, sizeof(pkt));
    nano_decode_event(pkt, n, &e);
    CHECK(e.kind == NANO_EV_EXP_ASSIGNMENTS && e.exp_assign.fx_range[0].assigned && e.exp_assign.fx_range[0].min == 0 && e.exp_assign.fx_range[0].max == 255);
    CHECK(e.exp_assign.fx_bypass_mode[4] == 2 && e.exp_assign.fx_bypass_mode[0] == 0 && e.exp_assign.capture_bypass && !e.exp_assign.ir_bypass && e.exp_assign.amp_ranges == 1);
    /* Request frame. */
    uint8_t m[16];
    const uint8_t want[] = { 0x08, 0xC0, 0x08, 0x03, 0x18, 0x3A, 0x3C, 0x00, 0x00, 0x00 };
    CHECK(nano_build_exp_assign_request(m, sizeof(m), 58) == 10 && memcmp(m, want, 10) == 0);
    CHECK(nano_build_exp_assign_request(m, sizeof(m), 64) == 0);
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
    test_expression();
    if (failures) {
        printf("%d failure(s)\n", failures);
        return 1;
    }
    printf("all protocol tests passed\n");
    return 0;
}
