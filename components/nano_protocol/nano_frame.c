#include "nano_frame.h"

#include <math.h>
#include <string.h>

const uint8_t NANO_REQ_METADATA[8] = { 0x06, 0xC0, 0x08, 0x03, 0x01, 0x00, 0x00, 0x00 };
const uint8_t NANO_REQ_STATE[14] = { 0x0C, 0xC0, 0x08, 0x03, 0x18, 0x01, 0x20, 0x01, 0x28, 0x01, 0x01, 0x00, 0x00, 0x00 };
const uint8_t NANO_REQ_SETTINGS[8] = { 0x06, 0xC0, 0x08, 0x03, 0x41, 0x00, 0x00, 0x00 };
const uint8_t NANO_REQ_TUNER_OFF[8] = { 0x06, 0xC0, 0x20, 0x00, 0x7F, 0x00, 0x00, 0x00 };

bool nano_parse_frame_header(const uint8_t *pkt, size_t len, nano_frame_header_t *out)
{
    if (len < 2) return false;
    uint16_t raw = (uint16_t)(pkt[0] | (pkt[1] << 8));
    uint16_t body = raw & NANO_LENGTH_MASK;
    if (body != len - 2) return false;
    if (out) {
        out->body_length = body;
        out->start = (raw & NANO_FLAG_START) != 0;
        out->end = (raw & NANO_FLAG_END) != 0;
    }
    return true;
}

size_t nano_split_trailer(const uint8_t *body, size_t len, int *msg_type)
{
    if (len >= 4 && body[len - 1] == 0 && body[len - 2] == 0 && body[len - 3] == 0) {
        if (msg_type) *msg_type = body[len - 4];
        return len - 4;
    }
    if (msg_type) *msg_type = -1;
    return len;
}

bool nano_is_tuner_pitch_packet(const uint8_t *pkt, size_t len)
{
    return len >= 8 && pkt[1] == 0xC0 && pkt[len - 4] == 0x80 && pkt[len - 3] == 0 && pkt[len - 2] == 0 && pkt[len - 1] == 0;
}

size_t nano_build_preset_select(uint8_t *out, size_t cap, uint8_t preset_index)
{
    static const uint8_t minus_one[10] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x01 };
    if (preset_index >= NANO_PRESET_COUNT || cap < NANO_PRESET_SELECT_LEN) return 0;
    uint8_t *p = out;
    *p++ = 0x36; *p++ = 0xC0;
    *p++ = 0x18; *p++ = 0x00;
    *p++ = 0x20; *p++ = preset_index;
    for (uint8_t tag = 0x28; tag <= 0x40; tag += 8) {
        *p++ = tag;
        memcpy(p, minus_one, 10);
        p += 10;
    }
    *p++ = 0x48; *p++ = 0x04;
    *p++ = 0x1D; *p++ = 0x00; *p++ = 0x00; *p++ = 0x00;
    return (size_t)(p - out);
}

static size_t bypass_frame(uint8_t *out, size_t cap, uint8_t selector, bool enabled)
{
    if (cap < 12) return 0;
    const uint8_t f[12] = { 0x0A, 0xC0, 0x08, 0x01, 0x18, selector, 0x20, enabled ? 0x00 : 0x01, 0x1F, 0x00, 0x00, 0x00 };
    memcpy(out, f, 12);
    return 12;
}

size_t nano_build_fx_bypass(uint8_t *out, size_t cap, uint8_t fx_slot, bool enabled)
{
    if (fx_slot >= NANO_FX_SLOT_COUNT) return 0;
    return bypass_frame(out, cap, (uint8_t)(0x04 + fx_slot), enabled);
}

size_t nano_build_gate_bypass(uint8_t *out, size_t cap, bool enabled)
{
    return bypass_frame(out, cap, 0x09, enabled);
}

size_t nano_build_tempo_set(uint8_t *out, size_t cap, float bpm)
{
    /* The per-tap shape with field 3 = 1 (verified on the pedal 2026-09-26). The shape without
     * field 3 (`0B C0 08 01 2D <f32> 91 …`) is ignored: no ack, state field 56 unchanged. */
    if (cap < 15 || bpm < 20.0f || bpm > 400.0f) return 0;
    uint8_t f[4];
    memcpy(f, &bpm, 4);
    const uint8_t frame[15] = { 0x0D, 0xC0, 0x08, 0x01, 0x18, 0x01, 0x2D, f[0], f[1], f[2], f[3], 0x91, 0x00, 0x00, 0x00 };
    memcpy(out, frame, 15);
    return 15;
}

size_t nano_build_tempo_exit(uint8_t *out, size_t cap, float bpm)
{
    if (cap < 13 || bpm < 20.0f || bpm > 400.0f) return 0;
    uint8_t f[4];
    memcpy(f, &bpm, 4);
    const uint8_t frame[13] = { 0x0B, 0xC0, 0x08, 0x01, 0x2D, f[0], f[1], f[2], f[3], 0x91, 0x00, 0x00, 0x00 };
    memcpy(out, frame, 13);
    return 13;
}

size_t nano_build_outputs_mute(uint8_t *out, size_t cap, bool mute)
{
    if (cap < 10) return 0;
    const uint8_t frame[10] = { 0x08, 0xC0, 0x08, 0x01, 0x68, mute ? 0x01 : 0x00, 0x43, 0x00, 0x00, 0x00 };
    memcpy(out, frame, 10);
    return 10;
}

size_t nano_build_capture_volume(uint8_t *out, size_t cap, uint8_t raw)
{
    size_t n = raw < 0x80 ? 12 : 13;
    if (cap < n) return 0;
    size_t i = 0;
    out[i++] = (uint8_t)(n - 2);
    out[i++] = 0xC0;
    out[i++] = 0x18; /* field 3: selector 10 */
    out[i++] = 0x0A;
    out[i++] = 0x20; /* field 4: the value */
    if (raw < 0x80) {
        out[i++] = raw;
    } else {
        out[i++] = (uint8_t)(raw | 0x80);
        out[i++] = 0x01;
    }
    out[i++] = 0x28; /* field 5 = 0 */
    out[i++] = 0x00;
    out[i++] = 0x1A;
    out[i++] = 0x00;
    out[i++] = 0x00;
    out[i++] = 0x00;
    return n;
}

#define CAP_VOL_MIN_DB (-24.0f)
#define CAP_VOL_SPAN_DB 36.0f
#define CAP_VOL_CURVE 1.708f

float nano_capture_volume_db(uint8_t raw)
{
    return CAP_VOL_SPAN_DB * powf(raw / 255.0f, 1.0f / CAP_VOL_CURVE) + CAP_VOL_MIN_DB;
}

uint8_t nano_capture_volume_raw(float db)
{
    float x = (db - CAP_VOL_MIN_DB) / CAP_VOL_SPAN_DB;
    if (x <= 0) return 0;
    if (x >= 1) return 255;
    return (uint8_t)lroundf(255.0f * powf(x, CAP_VOL_CURVE));
}

int nano_capture_volume_tenths(uint8_t raw)
{
    return (int)truncf(nano_capture_volume_db(raw) * 10.0f);
}

size_t nano_build_exp_assign_request(uint8_t *out, size_t cap, uint8_t preset_index)
{
    if (cap < 10 || preset_index >= NANO_PRESET_COUNT) return 0;
    const uint8_t frame[10] = { 0x08, 0xC0, 0x08, 0x03, 0x18, preset_index, 0x3C, 0x00, 0x00, 0x00 };
    memcpy(out, frame, 10);
    return 10;
}

size_t nano_build_tuner_on(uint8_t *out, size_t cap, float reference_hz, bool mute)
{
    if (cap < 17 || reference_hz < 400.0f || reference_hz > 480.0f) return 0;
    uint8_t f[4];
    memcpy(f, &reference_hz, 4); /* little-endian on ESP32 and every host we test on */
    const uint8_t frame[17] = { 0x0F, 0xC0, 0x20, 0x01, 0x2D, f[0], f[1], f[2], f[3], 0x30, 0x01, 0x38, mute ? 0x01 : 0x00, 0x7F, 0x00, 0x00, 0x00 };
    memcpy(out, frame, 17);
    return 17;
}
