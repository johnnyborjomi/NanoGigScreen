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
    int16_t capture_volume;            /* field 44, raw 0..255 (127 = the default level); -1 when absent */
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
    NANO_EV_EXP_VALUES,        /* values the pedal produced for its assigned targets (type 0xAA, with every position) */
    NANO_EV_EXP_ASSIGNMENTS,   /* a preset's expression assignments (type 0x3D, reply to nano_build_exp_assign_request) */
    NANO_EV_TUNER_PITCH,       /* note, cents, in tune */
    NANO_EV_TUNER_ACK,         /* on/off + reference */
    NANO_EV_SETTINGS,          /* device settings reply (type 0x42): outputs_muted */
    NANO_EV_OUTPUTS_MUTE_ACK,  /* ack to the outputs-mute write (type 0x44) */
    NANO_EV_TAP_TEMPO,         /* tempo while tapping / when the tap tempo mode ends (2026-09-26) */
} nano_event_kind_t;

/* One FX amount range on the pedal's 0..255 scale (Cortex Cloud shows 0..100 %). */
typedef struct {
    bool assigned;
    uint8_t min, max;
} nano_exp_range_t;

/*
 * A preset's expression pedal assignments as far as the screen shows them: the FX amount ranges
 * and the FX bypass switches (pre1, pre2, post1, post2, post3). Amp knobs, level and the
 * capture / IR bypasses are decoded only as flags for the log.
 */
typedef struct {
    nano_exp_range_t fx_range[NANO_FX_SLOT_COUNT];
    uint8_t fx_bypass_mode[NANO_FX_SLOT_COUNT]; /* 0 = none; 2 = heel-toe (flips at mid-travel); 1 / 3 = toe switch modes */
    bool capture_bypass, ir_bypass;
    uint8_t amp_ranges;                         /* how many of gain / bass / mid / treble / level are assigned */
} nano_exp_assignments_t;

/* What the pedal produced for the assigned FX (type 0xAA): -1 = not assigned / absent. */
typedef struct {
    int16_t fx_value[NANO_FX_SLOT_COUNT];  /* 0..255 after the range is applied */
    int8_t fx_bypass[NANO_FX_SLOT_COUNT];  /* 0 / 1 */
} nano_exp_values_t;

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
    bool outputs_muted;        /* SETTINGS field 16 (absent = outputs on) */
    uint8_t selector;          /* CONTROL from 0x1C: field 3 (1 capture bypass, 3 cab / IR slot, 4 capture slot), 0 when absent */
    int32_t value;             /* CONTROL from 0x1C: field 4, -1 when absent */
    nano_exp_values_t exp_values;         /* EXP_VALUES */
    nano_exp_assignments_t exp_assign;    /* EXP_ASSIGNMENTS */
} nano_event_t;

/* Decode a single-packet live message (full packet including the 2-byte header). */
void nano_decode_event(const uint8_t *pkt, size_t len, nano_event_t *out);

/* How the preset row names a preset (the pedal itself only counts 1..64). */
typedef enum {
    NANO_LABEL_NUMBER_LETTER = 0, /* "3B": bank number + slot letter (Mvave Chocolate) */
    NANO_LABEL_LETTER_NUMBER,     /* "B2": bank letter + slot number (other MIDI controllers) */
    NANO_LABEL_NUMERIC,           /* "10": the preset number, as on the Nano Cortex */
} nano_label_style_t;

/* Preset label for a zero-based index: index 9 → "3B" / "B2" (4 per bank) / "10". */
void nano_preset_label(uint8_t preset_index, uint8_t per_bank, nano_label_style_t style, char *out, size_t cap);

#ifdef __cplusplus
}
#endif
#endif
