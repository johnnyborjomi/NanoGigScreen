/*
 * Decoders for the state dump, the metadata dump and the live events.
 * Port of NanoGig `src/protocol/decode.ts`. Fixed-size structs, no
 * allocation; decoders never fault on unknown payloads.
 *
 * Provisional: reverse-engineered from NanOS 2.2.1 and liable to change.
 */
#ifndef NANO_DECODE_H
#define NANO_DECODE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "nano_frame.h"
#include "nano_models.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NANO_NAME_CAP 32       /* preset names are <= 20 chars; capture / IR short names fit in 31 */
#define NANO_MODEL_ID_CAP 8    /* longest catalogue ID is 6 hex chars */
#define NANO_CAPTURE_SLOTS 25
#define NANO_IR_SLOTS 5

typedef struct {
    char name[NANO_NAME_CAP];
    char capture_name[NANO_NAME_CAP];
    char ir_short_name[NANO_NAME_CAP];
} nano_preset_record_t;

typedef struct {
    nano_preset_record_t presets[NANO_PRESET_COUNT];
    char captures[NANO_CAPTURE_SLOTS][NANO_NAME_CAP];
    char irs[NANO_IR_SLOTS][NANO_NAME_CAP];
    uint8_t preset_record_count; /* records actually present (before padding) */
    uint8_t capture_count;
    uint8_t ir_count;
} nano_metadata_t;

/* Decode a reassembled metadata message body (trailer may still be attached). */
bool nano_decode_metadata(const uint8_t *body, size_t len, nano_metadata_t *out);

typedef struct {
    char id[NANO_MODEL_ID_CAP];        /* "" when the slot carries no model */
    const nano_fx_model_t *model;      /* NULL when unknown or empty */
} nano_fx_slot_t;

typedef struct {
    bool has_bypass;                   /* field 31 present */
    bool fx_on[NANO_FX_SLOT_COUNT];    /* pre1, pre2, post1, post2, post3 */
    bool gate_on;                      /* field 54 absent = on (inverted flag) */
    bool cab_on;                       /* field 12 present */
    bool capture_on;                   /* field 11 > 0 (position in the bank; 0 / absent = bypassed) */
    char capture_name[NANO_NAME_CAP];
    char ir_short_name[NANO_NAME_CAP];
    nano_fx_slot_t fx[NANO_FX_SLOT_COUNT];
    uint8_t active_preset;             /* field 13; absent = 0 = preset 1 */
    uint8_t footswitch[4];             /* IA, IB, IIA, IIB = fields 14, 15, 38, 39 */
    char firmware[16];                 /* field 24, "2.2.1" */
    float tempo_bpm;                   /* field 56, 0 when absent */
    float tuner_reference_hz;          /* field 46 (440.0 on the user's pedal), 0 when absent */
    uint8_t amp[5];                    /* gain, level, bass, mid, treble = fields 3..7 */
} nano_state_t;

/* Decode a state dump body. False when nothing recognisable is present. */
bool nano_decode_state(const uint8_t *body, size_t len, nano_state_t *out);

typedef enum {
    NANO_EV_UNKNOWN = 0,
    NANO_EV_PRESET_CHANGED,    /* preset + footswitch assignments */
    NANO_EV_BYPASS_CHANGED,
    NANO_EV_CONTROL,           /* knob / encoder / generic change: re-read state (debounced) */
    NANO_EV_PRESET_SELECT_ACK,
    NANO_EV_EXPRESSION,        /* position 0..254 */
    NANO_EV_TUNER_PITCH,       /* note, cents, in tune */
    NANO_EV_TUNER_ACK,         /* on/off + reference */
    NANO_EV_SETTINGS,
    NANO_EV_TAP_TEMPO,         /* tempo while tapping / when the tap tempo mode ends (2026-09-26) */
} nano_event_kind_t;

typedef struct {
    nano_event_kind_t kind;
    int msg_type;              /* trailer type, -1 when none */
    uint8_t preset;            /* PRESET_CHANGED */
    uint8_t footswitch[4];     /* PRESET_CHANGED */
    uint8_t position;          /* EXPRESSION */
    char note[4];              /* TUNER_PITCH: "A", "C#", ... */
    float cents;               /* TUNER_PITCH */
    bool in_tune;              /* TUNER_PITCH */
    bool tuner_on;             /* TUNER_ACK */
    bool tuner_muted;          /* TUNER_ACK field 7 (absent = 0 = outputs on) */
    bool tap_active;           /* TAP_TEMPO: field 3 = 1 while the mode is on; absent when it just ended */
    float tempo_bpm;           /* TAP_TEMPO: field 5 */
    float reference_hz;        /* TUNER_ACK, 0 when absent */
} nano_event_t;

/* Decode a single-packet live message (full packet including the 2-byte header). */
void nano_decode_event(const uint8_t *pkt, size_t len, nano_event_t *out);

/* Preset label in the Mvave Chocolate style ("3B": bank number + slot letter, 4 per bank). */
void nano_preset_label(uint8_t preset_index, uint8_t per_bank, char *out, size_t cap);

#ifdef __cplusplus
}
#endif
#endif
