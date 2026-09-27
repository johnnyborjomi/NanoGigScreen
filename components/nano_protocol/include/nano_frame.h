/*
 * c305 packet framing, message types and the byte-exact c304 request frames.
 * Port of NanoGig `src/protocol/reassembly.ts` + `frames.ts`. See
 * docs/PROTOCOL.md in the NanoGig repo before changing any byte here.
 *
 * Framing: byte[0..1] little-endian u16, bits 0-13 = body length
 * (= packet length - 2), bit 14 = START, bit 15 = END. Bodies end with a
 * 4-byte trailer `<msgType> 00 00 00`.
 */
#ifndef NANO_FRAME_H
#define NANO_FRAME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NANO_FLAG_START 0x4000u
#define NANO_FLAG_END 0x8000u
#define NANO_LENGTH_MASK 0x3fffu

/* The pedal sends 512-byte notifications: ask for this MTU right after connecting. */
#define NANO_PREFERRED_MTU 517

#define NANO_PRESET_COUNT 64
#define NANO_FX_SLOT_COUNT 5 /* pre1, pre2, post1, post2, post3 */

typedef struct {
    uint16_t body_length;
    bool start;
    bool end;
} nano_frame_header_t;

/* Parse the 2-byte header; false if the encoded length does not match the packet. */
bool nano_parse_frame_header(const uint8_t *pkt, size_t len, nano_frame_header_t *out);

/*
 * Split the `<msgType> 00 00 00` trailer off a message body. Returns the
 * payload length; *msg_type = -1 when there is no trailer.
 */
size_t nano_split_trailer(const uint8_t *body, size_t len, int *msg_type);

/* Trailer message types seen on NanOS 2.2.1. */
enum {
    NANO_MSG_DUMP = 0x02,            /* reply to a state / metadata request */
    NANO_MSG_KNOB = 0x1a,            /* knob turned (also tap tempo) */
    NANO_MSG_ENCODER = 0x1c,         /* footswitch encoder / bank button; ack to slot writes */
    NANO_MSG_PRESET_CHANGED = 0x1d,  /* field 4 = preset, 5..8 = footswitch assignments */
    NANO_MSG_PRESET_SELECT_ACK = 0x1e,
    NANO_MSG_BYPASS_CHANGED = 0x1f,
    NANO_MSG_EXP_ASSIGN_REPLY = 0x3d,
    NANO_MSG_EXPRESSION = 0x40,      /* pedal position 0..254 */
    NANO_MSG_SETTINGS = 0x42,
    NANO_MSG_OUTPUTS_MUTE_ACK = 0x44,
    NANO_MSG_CHANGED = 0x73,         /* generic "something changed" */
    NANO_MSG_TUNER = 0x7f,           /* tuner on/off (our write and the pedal's report) */
    NANO_MSG_TUNER_PITCH = 0x80,     /* ~30/s while a note sounds */
    NANO_MSG_TAP_TEMPO = 0x91,       /* tap tempo: field 3 = 1 while the mode is on, field 5 = BPM (f32); field 3 absent = mode left */
    NANO_MSG_EXPRESSION_VALUES = 0xaa,
};

/* Single-packet type-0x80 pitch reading (kept out of logs: ~30/s). */
bool nano_is_tuner_pitch_packet(const uint8_t *pkt, size_t len);

/* ---- c304 request frames (byte-exact, hardware-verified) ---------------- */

/* Metadata dump `06 C0 08 03 01 00 00 00`: ~17 KB over ~35 packets, type 0x02. */
extern const uint8_t NANO_REQ_METADATA[8];
/* Current state `0C C0 08 03 18 01 20 01 28 01 01 00 00 00`: 300-520 B, type 0x02. */
extern const uint8_t NANO_REQ_STATE[14];
/* Device settings `06 C0 08 03 41 00 00 00`: 60 B, type 0x42. */
extern const uint8_t NANO_REQ_SETTINGS[8];
/* Tuner off `06 C0 20 00 7F 00 00 00`. */
extern const uint8_t NANO_REQ_TUNER_OFF[8];

#define NANO_PRESET_SELECT_LEN 56
/*
 * Preset select over Bluetooth (Cortex Cloud's path, captured 2026-09-19, verified on the pedal):
 * `36 C0 18 00 20 <preset> 28 <-1> 30 <-1> 38 <-1> 40 <-1> 48 04 1D 00 00 00`, <-1> = FF×9 01.
 * The pedal answers `06 C0 08 01 1F 00 00 00` then `08 C0 08 01 20 01 1E 00 00 00`.
 * Returns bytes written (NANO_PRESET_SELECT_LEN) or 0 when the index is out of range.
 */
size_t nano_build_preset_select(uint8_t *out, size_t cap, uint8_t preset_index);

/* FX block on/off: `0A C0 08 01 18 <slot 4..8> 20 <0 on / 1 off> 1F 00 00 00`; slot 0..4 = pre1..post3. */
size_t nano_build_fx_bypass(uint8_t *out, size_t cap, uint8_t fx_slot, bool enabled);
/* Gate on/off: same frame with selector 9. */
size_t nano_build_gate_bypass(uint8_t *out, size_t cap, bool enabled);

/*
 * Tempo set: `0D C0 08 01 18 01 2D <f32 BPM> 91 00 00 00`, the pedal's own per-tap message
 * mirrored back (as the preset select mirrors 0x1D). Found by trial 2026-09-26: the pedal takes
 * it silently (no ack), enters its tap tempo mode, and the next state dump's field 56 carries
 * the new tempo. Without field 3 the tempo is not changed (see nano_build_tempo_exit).
 */
size_t nano_build_tempo_set(uint8_t *out, size_t cap, float bpm);
/* Leave tap tempo mode: `0B C0 08 01 2D <f32 BPM> 91 00 00 00`, the pedal's own exit message mirrored back. */
size_t nano_build_tempo_exit(uint8_t *out, size_t cap, float bpm);

/* Tuner on: `0F C0 20 01 2D <f32 Hz> 30 01 38 <mute> 7F 00 00 00`. */
size_t nano_build_tuner_on(uint8_t *out, size_t cap, float reference_hz, bool mute);

/*
 * Mute / unmute outputs 1/2 (Cortex Cloud's global "Mute Outputs 1/2"): `08 C0 08 01 68 <1 mute / 0 on>
 * 43 00 00 00`, byte-exact from a Cortex Cloud capture (2026-09-15; polarity checked by ear). The pedal
 * acks with type 0x44 and the settings reply's field 16 mirrors the value. Always 10 bytes.
 */
size_t nano_build_outputs_mute(uint8_t *out, size_t cap, bool mute);

/*
 * Read a preset's expression pedal assignments (Cortex Cloud's request on its Expression Pedal
 * page, captured 2026-09-19): `08 C0 08 03 18 <preset> 3C 00 00 00`. The reply is type 0x3D and
 * carries no preset number: remember which one was asked for.
 */
size_t nano_build_exp_assign_request(uint8_t *out, size_t cap, uint8_t preset_index);

#ifdef __cplusplus
}
#endif
#endif
