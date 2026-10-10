#include "nano_decode.h"
#include "nano_proto.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

/* ---- names ------------------------------------------------------------- */

/* >= 12 hex chars (dashes ignored) is an internal identifier, not a display name. */
static bool is_internal_identifier(const char *s)
{
    size_t n = 0;
    for (const char *p = s; *p; p++) {
        if (*p == '-') continue;
        if (!isxdigit((unsigned char)*p)) return false;
        n++;
    }
    return n >= 12;
}

/* Trim, blank identifier-like names (in place). */
static void sanitize_name(char *s)
{
    char *start = s;
    while (*start == ' ') start++;
    size_t n = strlen(start);
    while (n && start[n - 1] == ' ') n--;
    memmove(s, start, n);
    s[n] = '\0';
    if (n == 0 || is_internal_identifier(s)) s[0] = '\0';
}

static void string_field(const uint8_t *d, size_t len, uint32_t field, char *out, size_t cap)
{
    nano_get_text(d, len, field, out, cap);
    sanitize_name(out);
}

static uint8_t clamp_u8(uint64_t v) { return v > 255 ? 255 : (uint8_t)v; }

/* ---- metadata ---------------------------------------------------------- */

static bool metadata_from_top_level(const uint8_t *body, size_t len, nano_metadata_t *out)
{
    memset(out, 0, sizeof(*out));
    nano_proto_iter_t it;
    nano_field_t f;
    nano_proto_iter_init(&it, body, len);
    while (nano_proto_next(&it, &f)) {
        if (f.wire != NANO_WIRE_BYTES) continue;
        if (f.field == 18 && out->preset_record_count < NANO_PRESET_COUNT) {
            nano_preset_record_t *p = &out->presets[out->preset_record_count++];
            string_field(f.raw, f.len, 1, p->name, sizeof(p->name));
            string_field(f.raw, f.len, 7, p->capture_name, sizeof(p->capture_name));
            string_field(f.raw, f.len, 9, p->ir_short_name, sizeof(p->ir_short_name));
        } else if (f.field == 17 && out->capture_count < NANO_CAPTURE_SLOTS) {
            string_field(f.raw, f.len, 2, out->captures[out->capture_count], NANO_NAME_CAP);
            out->capture_count++;
        } else if (f.field == 19 && out->ir_count < NANO_IR_SLOTS) {
            char *slot = out->irs[out->ir_count];
            string_field(f.raw, f.len, 1, slot, NANO_NAME_CAP);
            if (!slot[0]) string_field(f.raw, f.len, 3, slot, NANO_NAME_CAP);
            if (slot[0]) out->ir_count++;
        }
    }
    return out->preset_record_count > 0;
}

/*
 * A real metadata dump carries all 64 preset records and is ~17 KB. A state dump is under
 * 1 KB and, re-parsed from a shifted offset, can produce one bogus "record" (seen 2026-09-26:
 * it wiped the name cache), so both checks are required before anything is treated as metadata.
 */
#define NANO_METADATA_MIN_RECORDS 2
#define NANO_METADATA_MIN_LEN 2048

bool nano_decode_metadata(const uint8_t *body, size_t len, nano_metadata_t *out)
{
    if (len < NANO_METADATA_MIN_LEN) return false;
    if (metadata_from_top_level(body, len, out) && out->preset_record_count >= NANO_METADATA_MIN_RECORDS) return true;
    /* Scan past a partial / non-protobuf prefix (rixrix FR-18). */
    size_t limit = len < 64 ? len : 64;
    for (size_t start = 1; start < limit; start++) {
        if (metadata_from_top_level(body + start, len - start, out) && out->preset_record_count >= NANO_METADATA_MIN_RECORDS) return true;
    }
    return false;
}

/* ---- state ------------------------------------------------------------- */

/* An FX slot's model id as hex. Both encodings seen: varint (raw value bytes) and length-delimited bytes. */
static void model_id_hex(const nano_field_t *f, nano_fx_slot_t *slot)
{
    if (f->wire != NANO_WIRE_VARINT && f->wire != NANO_WIRE_BYTES) return;
    if (f->len * 2 >= sizeof(slot->id)) return;
    static const char HEX[] = "0123456789ABCDEF";
    for (size_t i = 0; i < f->len; i++) {
        slot->id[i * 2] = HEX[f->raw[i] >> 4];
        slot->id[i * 2 + 1] = HEX[f->raw[i] & 0xf];
    }
    slot->id[f->len * 2] = '\0';
    slot->model = nano_lookup_fx_model(slot->id);
}

/* Footswitch assignments IA, IB, IIA, IIB. */
static const uint32_t FS_FIELDS[4] = { 14, 15, 38, 39 };

/*
 * One pass over the dump. The first occurrence of a field wins (as the field-by-field reads did); zero-valued
 * varints are left out by the pedal (proto3), so every field starts at its absent meaning.
 */
bool nano_decode_state(const uint8_t *body, size_t len, nano_state_t *out)
{
    memset(out, 0, sizeof(*out));
    int msg_type;
    size_t plen = nano_split_trailer(body, len, &msg_type);
    if (msg_type != NANO_MSG_DUMP && msg_type != -1) return false;
    out->gate_on = true;          /* field 54 is an inverted bypass flag: absent = on */
    out->capture_volume = -1;
    bool has_capture = false, has_ir = false, has_amp = false;
    uint64_t seen = 0;            /* fields below 64 already taken */
    nano_proto_iter_t it;
    nano_field_t f;
    nano_proto_iter_init(&it, body, plen);
    while (nano_proto_next(&it, &f)) {
        if (f.field < 64) {
            if (seen & (1ull << f.field)) continue;
            seen |= 1ull << f.field;
        }
        bool varint = f.wire == NANO_WIRE_VARINT, bytes = f.wire == NANO_WIRE_BYTES, f32 = f.wire == NANO_WIRE_FIXED32;
        switch (f.field) {
        case 3: case 4: case 5: case 6: case 7:  /* gain, level, bass, mid, treble */
            if (varint) {
                has_amp = true;
                out->amp[f.field - 3] = clamp_u8(f.value);
            }
            break;
        case 11:  /* capture position in the bank; 0 / absent = bypassed */
            out->capture_on = varint && f.value > 0;
            break;
        case 12:  /* IR slot: present = on */
            out->cab_on = true;
            if (varint && f.value > 0 && f.value < 256) out->cab_slot = (uint8_t)f.value;
            break;
        case 13:  /* absent = preset 1 */
            if (varint && f.value < NANO_PRESET_COUNT) out->active_preset = (uint8_t)f.value;
            break;
        case 14: case 15: case 38: case 39:
            for (int i = 0; i < 4; i++) {
                if (FS_FIELDS[i] == f.field && varint && f.value < NANO_PRESET_COUNT) out->footswitch[i] = (uint8_t)f.value;
            }
            break;
        case 24:
            nano_field_text(&f, out->firmware, sizeof(out->firmware));
            break;
        case 31:  /* FX bypass flags, one byte per slot */
            if (bytes && f.len >= NANO_FX_SLOT_COUNT) {
                out->has_bypass = true;
                for (int i = 0; i < NANO_FX_SLOT_COUNT; i++) out->fx_on[i] = f.raw[i] == 0;
            }
            break;
        case 32:
            if (bytes) {
                has_capture = true;
                string_field(f.raw, f.len, 2, out->capture_name, sizeof(out->capture_name));
            }
            break;
        case 33:
            if (bytes) {
                has_ir = true;
                string_field(f.raw, f.len, 2, out->ir_short_name, sizeof(out->ir_short_name));
            }
            break;
        case 44:
            if (varint) out->capture_volume = clamp_u8(f.value);
            break;
        case 46:
            if (f32) {
                float ref = nano_field_f32(&f);
                if (ref >= 400.0f && ref <= 480.0f) out->tuner_reference_hz = ref;
            }
            break;
        case 48: case 49: case 50: case 51: case 52:
            model_id_hex(&f, &out->fx[f.field - 48]);
            break;
        case 54:
            if (varint) out->gate_on = f.value == 0;
            break;
        case 56:
            if (f32) {
                float bpm = nano_field_f32(&f);
                if (bpm >= 20.0f && bpm <= 400.0f) out->tempo_bpm = bpm;
            }
            break;
        default:
            break;
        }
    }
    return out->has_bypass || has_capture || has_ir || has_amp;
}

/* ---- IR settings ------------------------------------------------------- */

bool nano_decode_cab_settings(const uint8_t *d, size_t len, nano_cab_settings_t *out)
{
    memset(out, 0, sizeof(*out));
    bool ir = false;
    nano_proto_iter_t it;
    nano_field_t f;
    nano_proto_iter_init(&it, d, len);
    while (nano_proto_next(&it, &f)) {
        if (f.wire != NANO_WIRE_BYTES) continue;
        if ((f.field == 5 || f.field == 6) && !ir) {
            ir = true;
            out->factory = f.field == 5;
            out->kind = nano_get_uint(f.raw, f.len, 1, UINT32_MAX, 0);
            nano_get_text(f.raw, f.len, 2, out->ir_name, sizeof(out->ir_name));
            int pos = nano_get_index(f.raw, f.len, 3, NANO_CAB_POSITIONS, 0);
            out->position = pos < 0 ? 0 : (uint8_t)pos;
            nano_get_text(f.raw, f.len, 4, out->mic, sizeof(out->mic));
        } else if (f.field == 7 && out->mic_count < NANO_CAB_MICS_MAX && f.len && f.len < NANO_CAB_MIC_CAP) {
            memcpy(out->mics[out->mic_count], f.raw, f.len);
            out->mics[out->mic_count][f.len] = '\0';
            out->mic_count++;
        } else if (f.field == 8) {
            nano_proto_iter_t in;
            nano_field_t g;
            nano_proto_iter_init(&in, f.raw, f.len);
            while (nano_proto_next(&in, &g)) {
                if (g.wire == NANO_WIRE_FIXED32 && g.field >= 1 && g.field <= 3) out->values[g.field - 1] = nano_field_f32(&g);
                else if (g.wire == NANO_WIRE_VARINT && g.field == 4) out->phase_inverted = g.value != 0;
            }
        }
    }
    return ir;
}

/* ---- FX parameters ------------------------------------------------------ */

int nano_decode_fx_params(const uint8_t *d, size_t len, float *values, int max)
{
    nano_proto_iter_t it;
    nano_field_t f;
    nano_proto_iter_init(&it, d, len);
    while (nano_proto_next(&it, &f)) {
        if (f.field != 4 || f.wire != NANO_WIRE_BYTES) continue;
        int n = 0;
        for (; n < max && (size_t)(n + 1) * 4 <= f.len; n++) memcpy(&values[n], f.raw + 4 * n, sizeof(float));
        return n;
    }
    return 0;
}

/* ---- IR library ---------------------------------------------------------- */

size_t nano_decode_ir_library(const uint8_t *d, size_t len, nano_ir_library_t *out, size_t cap)
{
    static const uint32_t FIELDS[2] = { 4, 6 }; /* NANO_IR_FACTORY, NANO_IR_USER */
    uint32_t count[2] = { 0 }, size = 0;
    /* One pass per list: the factory names first whatever the order on the wire. */
    for (int list = 0; list < 2; list++) {
        nano_proto_iter_t it;
        nano_field_t f;
        nano_proto_iter_init(&it, d, len);
        while (nano_proto_next(&it, &f)) {
            if (f.wire != NANO_WIRE_BYTES || f.field != FIELDS[list]) continue;
            size_t at = sizeof(*out) + size;
            if (out && at + f.len + 1 <= cap) {
                memcpy(out->names + size, f.raw, f.len);
                out->names[size + f.len] = '\0';
            }
            size += (uint32_t)f.len + 1;
            count[list]++;
        }
    }
    if (count[0] + count[1] == 0 || size > UINT16_MAX || count[0] > UINT16_MAX || count[1] > UINT16_MAX) return 0;
    size_t need = sizeof(*out) + size;
    if (out && need <= cap) {
        out->count[0] = (uint16_t)count[0];
        out->count[1] = (uint16_t)count[1];
        out->size = (uint16_t)size;
    }
    return need;
}

const char *nano_ir_library_name(const nano_ir_library_t *lib, int list, int index)
{
    if (!lib || list < 0 || list > 1 || index < 0 || index >= lib->count[list]) return NULL;
    int skip = (list == NANO_IR_USER ? lib->count[0] : 0) + index;
    const char *p = lib->names;
    while (skip-- > 0) p += strlen(p) + 1;
    return p;
}

/* ---- events ------------------------------------------------------------ */

void nano_decode_event(const uint8_t *pkt, size_t len, nano_event_t *out)
{
    memset(out, 0, sizeof(*out));
    out->kind = NANO_EV_UNKNOWN;
    out->msg_type = -1;
    nano_frame_header_t h;
    if (!nano_parse_frame_header(pkt, len, &h)) return;
    const uint8_t *body = pkt + 2;
    int msg_type;
    size_t plen = nano_split_trailer(body, len - 2, &msg_type);
    out->msg_type = msg_type;
    switch (msg_type) {
    case NANO_MSG_PRESET_CHANGED: {
        /* `10 C0 08 01 20 <preset> 28 <IA> 30 <IB> 38 <IIA> 40 <IIB> 1D 00 00 00`; zero fields absent. */
        int preset = nano_get_index(body, plen, 4, NANO_PRESET_COUNT, 0);
        if (preset < 0) return;
        out->kind = NANO_EV_PRESET_CHANGED;
        out->preset = (uint8_t)preset;
        for (uint32_t i = 0; i < 4; i++) {
            int v = nano_get_index(body, plen, 5 + i, NANO_PRESET_COUNT, 0);
            out->footswitch[i] = v < 0 ? 0 : (uint8_t)v;
        }
        return;
    }
    case NANO_MSG_BYPASS_CHANGED:
        out->kind = NANO_EV_BYPASS_CHANGED;
        return;
    case NANO_MSG_PRESET_SELECT_ACK:
        out->kind = NANO_EV_PRESET_SELECT_ACK;
        return;
    case NANO_MSG_ENCODER: {
        /* `18 <sel> 20 <val> 1C`: the same shape as the slot writes, sent when the encoder scrolls captures / IRs. */
        out->kind = NANO_EV_CONTROL;
        int sel = nano_get_index(body, plen, 3, 256, 0);
        out->selector = sel < 0 ? 0 : (uint8_t)sel;
        out->value = nano_get_index(body, plen, 4, 1000, -1);
        return;
    }
    case NANO_MSG_KNOB:
    case NANO_MSG_CHANGED:
        out->kind = NANO_EV_CONTROL;
        out->value = -1;
        return;
    case NANO_MSG_EXPRESSION: {
        out->kind = NANO_EV_EXPRESSION;
        out->position = (uint8_t)nano_get_uint(body, plen, 4, 254, 0); /* absent at heel */
        return;
    }
    case NANO_MSG_EXPRESSION_VALUES: {
        /* One varint per assigned target: FX amounts pre1..post3 at fields 9..13 (0..255), FX bypass
         * flags at 17..21; `06 C0 08 01 AA 00 00 00` after a preset load with nothing assigned. */
        out->kind = NANO_EV_EXP_VALUES;
        for (uint32_t i = 0; i < NANO_FX_SLOT_COUNT; i++) {
            uint64_t v;
            out->exp_values.fx_value[i] = nano_get_varint(body, plen, 9 + i, &v) ? clamp_u8(v) : -1;
            out->exp_values.fx_bypass[i] = nano_get_varint(body, plen, 17 + i, &v) ? (v != 0) : -1;
        }
        return;
    }
    case NANO_MSG_EXP_ASSIGN_REPLY: {
        /* `08 01` then one sub-message per assigned target, numbered one below Cortex Cloud's write:
         * ranges `{1: flag, 2: min, 3: max}` at 3..6 (gain, bass, mid, treble), 7..11 (pre1..post3),
         * 20 (level); bypasses `{<mode>: {...}}` at 13 (capture), 14 (IR), 15..19 (pre1..post3). */
        out->kind = NANO_EV_EXP_ASSIGNMENTS;
        nano_exp_assignments_t *a = &out->exp_assign;
        const uint8_t *sub;
        size_t sub_len;
        for (uint32_t i = 0; i < NANO_FX_SLOT_COUNT; i++) {
            if (nano_get_bytes(body, plen, 7 + i, &sub, &sub_len)) {
                a->fx_range[i].assigned = true;
                a->fx_range[i].min = (uint8_t)nano_get_uint(sub, sub_len, 2, 255, 0);
                a->fx_range[i].max = (uint8_t)nano_get_uint(sub, sub_len, 3, 255, 255);
                if (a->fx_range[i].max < a->fx_range[i].min) a->fx_range[i].max = a->fx_range[i].min;
            }
            if (nano_get_bytes(body, plen, 15 + i, &sub, &sub_len)) {
                nano_field_t f;
                nano_proto_iter_t it;
                nano_proto_iter_init(&it, sub, sub_len);
                a->fx_bypass_mode[i] = nano_proto_next(&it, &f) && f.wire == NANO_WIRE_BYTES && f.field < 255 ? (uint8_t)f.field : 2;
                if (!a->fx_bypass_mode[i]) a->fx_bypass_mode[i] = 2;
            }
        }
        a->capture_bypass = nano_has_field(body, plen, 13);
        a->ir_bypass = nano_has_field(body, plen, 14);
        for (uint32_t f = 3; f <= 6; f++) a->amp_ranges += nano_has_field(body, plen, f);
        a->amp_ranges += nano_has_field(body, plen, 20);
        return;
    }
    case NANO_MSG_TUNER_PITCH: {
        float cents;
        if (!nano_get_text(body, plen, 4, out->note, sizeof(out->note)) || !nano_get_f32(body, plen, 5, &cents)) return;
        out->kind = NANO_EV_TUNER_PITCH;
        out->cents = cents;
        out->in_tune = nano_get_uint(body, plen, 7, 2, 0) == 1;
        return;
    }
    case NANO_MSG_TUNER: {
        out->kind = NANO_EV_TUNER_ACK;
        out->tuner_on = nano_get_uint(body, plen, 4, 2, 0) == 1;
        out->tuner_muted = nano_get_uint(body, plen, 7, 2, 0) == 1;
        float ref;
        out->reference_hz = nano_get_f32(body, plen, 5, &ref) ? ref : 0.0f;
        return;
    }
    case NANO_MSG_SETTINGS:
        /* 60-byte reply to NANO_REQ_SETTINGS; field 16 = 1 while outputs 1/2 are muted, absent while on. */
        out->kind = NANO_EV_SETTINGS;
        out->outputs_muted = nano_get_uint(body, plen, 16, 2, 0) == 1;
        return;
    case NANO_MSG_RENAME_REPLY: {
        /* `08 06 18 <preset> 20 <1 = ok>` (2026-10-08); absent preset = preset 1 (proto3 leaves zeros out). */
        int preset = nano_get_index(body, plen, 3, NANO_PRESET_COUNT, 0);
        if (preset < 0) return;
        out->kind = NANO_EV_RENAME_REPLY;
        out->preset = (uint8_t)preset;
        out->ok = nano_get_uint(body, plen, 4, 2, 0) == 1;
        return;
    }
    case NANO_MSG_OUTPUTS_MUTE_ACK:
        out->kind = NANO_EV_OUTPUTS_MUTE_ACK;
        return;
    case NANO_MSG_TAP_TEMPO: {
        /* `0D C0 08 01 18 01 2D <f32 BPM> 91 00 00 00` per tap; `0B C0 08 01 2D <f32> 91 00 00 00` on exit
         * (captured 2026-09-26 on the user's pedal: hold the left switch, tap, hold again). */
        float bpm;
        if (!nano_get_f32(body, plen, 5, &bpm) || bpm < 20.0f || bpm > 400.0f) return;
        out->kind = NANO_EV_TAP_TEMPO;
        out->tap_active = nano_get_uint(body, plen, 3, 2, 0) == 1;
        out->tempo_bpm = bpm;
        return;
    }
    default:
        return;
    }
}

/* A..Z, then AA, AB, ...: any bank count over 64 presets gets a name. */
static void letters(unsigned n, char *out, size_t cap)
{
    char tmp[4];
    size_t k = 0;
    int v = (int)n;
    do {
        tmp[k++] = (char)('A' + v % 26);
        v = v / 26 - 1;
    } while (v >= 0 && k < sizeof(tmp));
    size_t i = 0;
    while (k > 0 && i + 1 < cap) out[i++] = tmp[--k];
    out[i] = '\0';
}

void nano_preset_label(uint8_t preset_index, uint8_t per_bank, nano_label_style_t style, char *out, size_t cap)
{
    if (per_bank == 0) per_bank = 4;
    if (preset_index >= NANO_PRESET_COUNT) {
        snprintf(out, cap, "-");
        return;
    }
    unsigned bank = preset_index / per_bank;
    unsigned slot = preset_index % per_bank;
    char l[4];
    switch (style) {
    case NANO_LABEL_LETTER_NUMBER:
        letters(bank, l, sizeof(l));
        snprintf(out, cap, "%s%u", l, slot + 1);
        return;
    case NANO_LABEL_NUMERIC:
        snprintf(out, cap, "%u", (unsigned)preset_index + 1);
        return;
    default:
        letters(slot, l, sizeof(l));
        snprintf(out, cap, "%u%s", bank + 1, l);
        return;
    }
}
