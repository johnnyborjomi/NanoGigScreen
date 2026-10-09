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
    NANO_MSG_OUTPUTS_MUTE_ACK = 0x44, /* any settings write answers this (UpdateSettingsResponse) */
    NANO_MSG_CAB_SETTING = 0x5e,     /* nano_build_cab_setting */
    NANO_MSG_CAB_SETTINGS = 0x60,    /* reply to nano_build_cab_settings_request (nano_decode_cab_settings) */
    NANO_MSG_RENAME_REPLY = 0x70,    /* reply to nano_build_preset_rename */
    NANO_MSG_CHANGED = 0x73,         /* unsaved changes on / off (field 3) */
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
 * Neural capture volume (Cortex Cloud's capture volume slider, captured 2026-10-07 from an Android HCI
 * snoop log): `0A C0 18 0A 20 <raw varint> 28 00 1A 00 00 00`, 0B and a two-byte varint from 128 up.
 * Raw 0..255 = -24..+12 dB on a curve (see nano_capture_volume_db); state field 44 carries it back.
 * A live edit: it changes the sound, it does not save the preset. Returns 12 or 13.
 */
size_t nano_build_capture_volume(uint8_t *out, size_t cap, uint8_t raw);
/*
 * Cortex Cloud's capture volume scale, fitted to five readings (2026-10-07: 0 = -24, 39 = -12.0,
 * 102 = -2.9, 128 = 0.0, 255 = +12 dB): raw = 255 * ((dB + 24) / 36)^1.708, each within 0.05 dB.
 */
float nano_capture_volume_db(uint8_t raw);
uint8_t nano_capture_volume_raw(float db);
/* What Cortex Cloud shows for `raw`, in tenths of a dB: the value cut toward zero, not rounded
 * (raw 101 = -3.07 dB reads "-3.0" there; all five readings above agree), with 0.01 dB of slack
 * for the fit at the boundaries (raw 110 = -1.995 reads -2.0, not -1.9). */
int nano_capture_volume_tenths(uint8_t raw);

/*
 * Rename a preset (the pedal's RenamePreset, frame from DrD85/nano-cortex-controller, verified on the
 * user's pedal 2026-10-08): `<len> C0 08 01 18 <preset> 22 <n> <name> 6F 00 00 00`. The pedal stores the
 * name at once (no save needed) and answers type 0x70 `08 06 18 <preset> 20 <1 = ok>`. The pedal itself
 * checks nothing (2026-10-08: it took "", 3 characters, duplicates, spaces at the ends, 32 characters,
 * UTF-8), so the app keeps DrD85's rules: 4 to 32 characters, no duplicates; 31 here = the name cache.
 * Returns bytes written, or 0 for an empty name, one longer than NANO_PRESET_NAME_MAX or a bad index.
 */
#define NANO_PRESET_NAME_MAX 31
size_t nano_build_preset_rename(uint8_t *out, size_t cap, uint8_t preset_index, const char *name);

/*
 * IR (cab) of the current preset: Cortex Cloud's IR loader. Read and level / filter writes from
 * DrD85/nano-cortex-controller (MIT); phase, microphone and position writes and the scales from an Android
 * HCI snoop of Cortex Cloud (2026-10-09). All writes are live edits: audible, the preset is not saved.
 *   read      `08 C0 18 00 20 <slot - 1> 5F 00 00 00` (slot = state field 12) -> type 0x60, see
 *             nano_decode_cab_settings.
 *   level etc `09 C0 <field 5 / 6 / 7 = level / high pass / low pass, f32 0..1> 5E 00 00 00`
 *   phase     `06 C0 40 <1 inverted / 0> 5E 00 00 00`
 *   mic       `<len> C0 1A <n> { 08 <kind> 12 <IR name> 18 <position 0..5> 22 <microphone> } 5E 00 00 00`:
 *             the factory IR is picked by name, position and microphone (both from the read).
 * Scales (Cortex Cloud's readouts, 2026-10-08/09): Level -96 + 108 * n^(1/3.5) dB (0 dB at 0.66212); High pass
 * 20 + 780 * n^(5/3) Hz; Low pass 1000 + 19000 * n^(5/3) Hz.
 */
typedef enum { NANO_CAB_LEVEL = 0, NANO_CAB_HIGH_PASS, NANO_CAB_LOW_PASS, NANO_CAB_PARAMS } nano_cab_param_t;
#define NANO_CAB_LEVEL_MIN_DB (-96.0f)
#define NANO_CAB_LEVEL_MAX_DB 12.0f
#define NANO_CAB_HIGH_PASS_MIN_HZ 20.0f
#define NANO_CAB_HIGH_PASS_MAX_HZ 800.0f
#define NANO_CAB_LOW_PASS_MIN_HZ 1000.0f
#define NANO_CAB_LOW_PASS_MAX_HZ 20000.0f
#define NANO_CAB_POSITIONS 6 /* Cortex Cloud's 1..6, from the cone's centre to its edge (0..5 on the wire) */
/* Returns 10, or 0 for slot 0 (state field 12: 1..5, 6 for most presets). */
size_t nano_build_cab_settings_request(uint8_t *out, size_t cap, uint8_t slot);
/* Returns 11 (the value is clamped to 0..1), or 0 for an unknown parameter. */
size_t nano_build_cab_setting(uint8_t *out, size_t cap, nano_cab_param_t param, float normalized);
/* Returns 8. */
size_t nano_build_cab_phase(uint8_t *out, size_t cap, bool inverted);
/* `kind` and `ir_name` as the read reported them (nano_cab_settings_t). Returns bytes written, 0 when too long. */
size_t nano_build_cab_mic(uint8_t *out, size_t cap, uint32_t kind, const char *ir_name, uint8_t position, const char *mic);
/* The pedal's 0..1 as dB (Level) or Hz (filters), and back (clamped). */
float nano_cab_value(nano_cab_param_t param, float normalized);
float nano_cab_normalized(nano_cab_param_t param, float value);

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
