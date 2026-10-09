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

#include "nano_proto.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NANO_FLAG_START 0x4000u
#define NANO_FLAG_END 0x8000u
#define NANO_LENGTH_MASK 0x3fffu

/* The pedal sends 512-byte notifications: ask for this MTU right after connecting. */
#define NANO_PREFERRED_MTU 517

/* Room for any frame this firmware writes (the longest is an IR microphone choice, under 140 B). */
#define NANO_FRAME_MAX 160

#define NANO_PRESET_COUNT 64
#define NANO_PRESET_NAME_MAX 31 /* longest name we write (nano_build_preset_rename) and cache */
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

/* Trailer message types seen on NanOS 2.2.1 (ours and the pedal's). */
enum {
    NANO_MSG_DUMP = 0x02,            /* reply to a state / metadata request */
    NANO_MSG_KNOB = 0x1a,            /* knob turned (also tap tempo) */
    NANO_MSG_ENCODER = 0x1c,         /* footswitch encoder / bank button; ack to slot writes */
    NANO_MSG_PRESET_CHANGED = 0x1d,  /* field 4 = preset, 5..8 = footswitch assignments */
    NANO_MSG_PRESET_SELECT_ACK = 0x1e,
    NANO_MSG_BYPASS_CHANGED = 0x1f,
    NANO_MSG_EXP_ASSIGN_REQUEST = 0x3c, /* nano_build_exp_assign_request */
    NANO_MSG_EXP_ASSIGN_REPLY = 0x3d,
    NANO_MSG_EXPRESSION = 0x40,      /* pedal position 0..254 */
    NANO_MSG_SETTINGS = 0x42,
    NANO_MSG_SETTINGS_UPDATE = 0x43, /* nano_build_outputs_mute */
    NANO_MSG_OUTPUTS_MUTE_ACK = 0x44, /* any settings write answers this (UpdateSettingsResponse) */
    NANO_MSG_CAB_SETTING = 0x5e,     /* nano_build_cab_setting */
    NANO_MSG_CAB_SETTINGS_REQUEST = 0x5f, /* nano_build_cab_settings_request */
    NANO_MSG_CAB_SETTINGS = 0x60,    /* reply to nano_build_cab_settings_request (nano_decode_cab_settings) */
    NANO_MSG_RENAME = 0x6f,          /* nano_build_preset_rename */
    NANO_MSG_RENAME_REPLY = 0x70,    /* reply to nano_build_preset_rename */
    NANO_MSG_CHANGED = 0x73,         /* unsaved changes on / off (field 3) */
    NANO_MSG_TUNER = 0x7f,           /* tuner on/off (our write and the pedal's report) */
    NANO_MSG_TUNER_PITCH = 0x80,     /* ~30/s while a note sounds */
    NANO_MSG_TAP_TEMPO = 0x91,       /* tap tempo: field 3 = 1 while the mode is on, field 5 = BPM (f32); field 3 absent = mode left */
    NANO_MSG_EXPRESSION_VALUES = 0xaa,
};

/* Single-packet type-0x80 pitch reading (kept out of logs: ~30/s). */
bool nano_is_tuner_pitch_packet(const uint8_t *pkt, size_t len);

/*
 * Writing a single-packet frame: begin leaves room for the header, end appends the `<type> 00 00 00` trailer and
 * fills in the START | END header. end returns the frame length, 0 when it did not fit (or a write failed).
 */
void nano_frame_begin(nano_pb_writer_t *w, uint8_t *out, size_t cap);
size_t nano_frame_end(nano_pb_writer_t *w, uint8_t msg_type);

#ifdef __cplusplus
}
#endif
#endif
