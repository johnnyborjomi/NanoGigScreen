#include "nano_build.h"
#include "nano_decode.h"

#include <string.h>

/* Field numbers and message types are written as the captures show them (`18` = field 3 varint, ...). */

const uint8_t NANO_REQ_METADATA[8] = { 0x06, 0xC0, 0x08, 0x03, 0x01, 0x00, 0x00, 0x00 };
const uint8_t NANO_REQ_STATE[14] = { 0x0C, 0xC0, 0x08, 0x03, 0x18, 0x01, 0x20, 0x01, 0x28, 0x01, 0x01, 0x00, 0x00, 0x00 };
const uint8_t NANO_REQ_SETTINGS[8] = { 0x06, 0xC0, 0x08, 0x03, 0x41, 0x00, 0x00, 0x00 };
const uint8_t NANO_REQ_TUNER_OFF[8] = { 0x06, 0xC0, 0x20, 0x00, 0x7F, 0x00, 0x00, 0x00 };

/* Field 1 of most writes: 1 = update (3 = read). */
#define OP_UPDATE 1
#define OP_READ 3

size_t nano_build_preset_select(uint8_t *out, size_t cap, uint8_t preset_index)
{
    if (preset_index >= NANO_PRESET_COUNT) return 0;
    nano_pb_writer_t w;
    nano_frame_begin(&w, out, cap);
    nano_pb_varint(&w, 3, 0);
    nano_pb_varint(&w, 4, preset_index);
    for (uint32_t field = 5; field <= 8; field++) nano_pb_int(&w, field, -1);
    nano_pb_varint(&w, 9, 4);
    return nano_frame_end(&w, NANO_MSG_PRESET_CHANGED);
}

static size_t bypass_frame(uint8_t *out, size_t cap, uint8_t selector, bool enabled)
{
    nano_pb_writer_t w;
    nano_frame_begin(&w, out, cap);
    nano_pb_varint(&w, 1, OP_UPDATE);
    nano_pb_varint(&w, 3, selector);
    nano_pb_varint(&w, 4, enabled ? 0 : 1);
    return nano_frame_end(&w, NANO_MSG_BYPASS_CHANGED);
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

static size_t tempo_frame(uint8_t *out, size_t cap, float bpm, bool tap)
{
    if (bpm < 20.0f || bpm > 400.0f) return 0;
    nano_pb_writer_t w;
    nano_frame_begin(&w, out, cap);
    nano_pb_varint(&w, 1, OP_UPDATE);
    if (tap) nano_pb_varint(&w, 3, 1);
    nano_pb_f32(&w, 5, bpm);
    return nano_frame_end(&w, NANO_MSG_TAP_TEMPO);
}

size_t nano_build_tempo_set(uint8_t *out, size_t cap, float bpm)
{
    /* The per-tap shape with field 3 = 1 (verified on the pedal 2026-09-26). The shape without
     * field 3 (`0B C0 08 01 2D <f32> 91 …`) is ignored: no ack, state field 56 unchanged. */
    return tempo_frame(out, cap, bpm, true);
}

size_t nano_build_tempo_exit(uint8_t *out, size_t cap, float bpm)
{
    return tempo_frame(out, cap, bpm, false);
}

size_t nano_build_outputs_mute(uint8_t *out, size_t cap, bool mute)
{
    nano_pb_writer_t w;
    nano_frame_begin(&w, out, cap);
    nano_pb_varint(&w, 1, OP_UPDATE);
    nano_pb_varint(&w, 13, mute ? 1 : 0);
    return nano_frame_end(&w, NANO_MSG_SETTINGS_UPDATE);
}

size_t nano_build_capture_volume(uint8_t *out, size_t cap, uint8_t raw)
{
    nano_pb_writer_t w;
    nano_frame_begin(&w, out, cap);
    nano_pb_varint(&w, 3, 10); /* selector 10: capture volume */
    nano_pb_varint(&w, 4, raw);
    nano_pb_varint(&w, 5, 0);
    return nano_frame_end(&w, NANO_MSG_KNOB);
}

size_t nano_build_exp_assign_request(uint8_t *out, size_t cap, uint8_t preset_index)
{
    if (preset_index >= NANO_PRESET_COUNT) return 0;
    nano_pb_writer_t w;
    nano_frame_begin(&w, out, cap);
    nano_pb_varint(&w, 1, OP_READ);
    nano_pb_varint(&w, 3, preset_index);
    return nano_frame_end(&w, NANO_MSG_EXP_ASSIGN_REQUEST);
}

size_t nano_build_preset_rename(uint8_t *out, size_t cap, uint8_t preset_index, const char *name)
{
    size_t n = name ? strlen(name) : 0;
    if (n == 0 || n > NANO_PRESET_NAME_MAX || preset_index >= NANO_PRESET_COUNT) return 0;
    nano_pb_writer_t w;
    nano_frame_begin(&w, out, cap);
    nano_pb_varint(&w, 1, OP_UPDATE);
    nano_pb_varint(&w, 3, preset_index);
    nano_pb_bytes(&w, 4, name, n);
    return nano_frame_end(&w, NANO_MSG_RENAME);
}

size_t nano_build_tuner_on(uint8_t *out, size_t cap, float reference_hz, bool mute)
{
    if (reference_hz < 400.0f || reference_hz > 480.0f) return 0;
    nano_pb_writer_t w;
    nano_frame_begin(&w, out, cap);
    nano_pb_varint(&w, 4, 1);
    nano_pb_f32(&w, 5, reference_hz);
    nano_pb_varint(&w, 6, 1);
    nano_pb_varint(&w, 7, mute ? 1 : 0);
    return nano_frame_end(&w, NANO_MSG_TUNER);
}

size_t nano_build_cab_select(uint8_t *out, size_t cap, uint8_t slot)
{
    if (slot > 0x7F) return 0;
    nano_pb_writer_t w;
    nano_frame_begin(&w, out, cap);
    nano_pb_varint(&w, 3, 3); /* selector 3: the IR */
    nano_pb_varint(&w, 4, slot);
    return nano_frame_end(&w, NANO_MSG_ENCODER);
}

size_t nano_build_cab_load(uint8_t *out, size_t cap, int list, uint32_t index, const char *name)
{
    if (!name || !name[0] || (list != NANO_IR_FACTORY && list != NANO_IR_USER)) return 0;
    nano_pb_writer_t w;
    nano_frame_begin(&w, out, cap);
    size_t ir = nano_pb_begin(&w, list == NANO_IR_FACTORY ? 3 : 4);
    nano_pb_varint(&w, 1, index);
    nano_pb_text(&w, 2, name);
    nano_pb_end(&w, ir);
    return nano_frame_end(&w, NANO_MSG_CAB_SETTING);
}

size_t nano_build_library_request(uint8_t *out, size_t cap, int what)
{
    if (!(what & (NANO_LIB_CAPTURES | NANO_LIB_IRS))) return 0;
    nano_pb_writer_t w;
    nano_frame_begin(&w, out, cap);
    if (what & NANO_LIB_CAPTURES) nano_pb_varint(&w, 3, 1); /* factory captures */
    if (what & NANO_LIB_IRS) nano_pb_varint(&w, 4, 1);      /* factory IRs */
    if (what & NANO_LIB_CAPTURES) nano_pb_varint(&w, 5, 1); /* user captures */
    if (what & NANO_LIB_IRS) nano_pb_varint(&w, 6, 1);      /* user IRs */
    return nano_frame_end(&w, NANO_MSG_LIBRARY_REQUEST);
}

/* `field` { 1: index, 2: name }: how every load names a library item. */
static void library_item(nano_pb_writer_t *w, uint32_t field, uint32_t index, const char *name)
{
    size_t item = nano_pb_begin(w, field);
    nano_pb_varint(w, 1, index);
    nano_pb_text(w, 2, name);
    nano_pb_end(w, item);
}

static bool item_ok(int list, const char *name) { return name && name[0] && (list == NANO_IR_FACTORY || list == NANO_IR_USER); }

size_t nano_build_capture_preview(uint8_t *out, size_t cap, int list, uint32_t index, const char *name)
{
    if (!item_ok(list, name)) return 0;
    nano_pb_writer_t w;
    nano_frame_begin(&w, out, cap);
    library_item(&w, list == NANO_IR_FACTORY ? 3 : 4, index, name);
    nano_pb_varint(&w, 8, 1);
    return nano_frame_end(&w, NANO_MSG_CAPTURE_PREVIEW);
}

static size_t slot_load(uint8_t *out, size_t cap, uint8_t slot, int list, uint32_t index, const char *name, int type)
{
    nano_pb_writer_t w;
    nano_frame_begin(&w, out, cap);
    nano_pb_varint(&w, 3, slot);
    library_item(&w, list == NANO_IR_FACTORY ? 4 : 5, index, name);
    return nano_frame_end(&w, type);
}

size_t nano_build_capture_slot_load(uint8_t *out, size_t cap, uint8_t slot, int list, uint32_t index, const char *name)
{
    if (!item_ok(list, name) || slot >= NANO_CAPTURE_SLOTS) return 0;
    return slot_load(out, cap, slot, list, index, name, NANO_MSG_CAPTURE_SLOT_LOAD);
}

size_t nano_build_cab_slot_load(uint8_t *out, size_t cap, uint8_t slot, int list, uint32_t index, const char *name)
{
    if (!item_ok(list, name) || slot >= NANO_IR_SLOTS) return 0;
    return slot_load(out, cap, slot, list, index, name, NANO_MSG_CAB_SLOT_LOAD);
}

size_t nano_build_capture_select(uint8_t *out, size_t cap, uint8_t slot)
{
    if (slot > NANO_CAPTURE_SLOTS) return 0;
    nano_pb_writer_t w;
    nano_frame_begin(&w, out, cap);
    nano_pb_varint(&w, 3, 1); /* selector 1: the capture, by its slot */
    nano_pb_varint(&w, 4, slot);
    return nano_frame_end(&w, NANO_MSG_ENCODER);
}

size_t nano_build_gate_threshold(uint8_t *out, size_t cap, uint8_t raw)
{
    nano_pb_writer_t w;
    nano_frame_begin(&w, out, cap);
    nano_pb_varint(&w, 3, 11); /* selector 11: gate threshold */
    nano_pb_varint(&w, 4, raw);
    nano_pb_varint(&w, 5, 0);
    return nano_frame_end(&w, NANO_MSG_KNOB);
}

size_t nano_build_cab_settings_request(uint8_t *out, size_t cap, uint8_t slot)
{
    if (slot < 1 || slot > 0x7F) return 0;
    nano_pb_writer_t w;
    nano_frame_begin(&w, out, cap);
    nano_pb_varint(&w, 3, 0);
    nano_pb_varint(&w, 4, slot - 1);
    return nano_frame_end(&w, NANO_MSG_CAB_SETTINGS_REQUEST);
}

size_t nano_build_cab_setting(uint8_t *out, size_t cap, nano_cab_param_t param, float normalized)
{
    if (param < 0 || param >= NANO_CAB_PARAMS) return 0;
    float n = normalized < 0 ? 0 : normalized > 1 ? 1 : normalized;
    nano_pb_writer_t w;
    nano_frame_begin(&w, out, cap);
    nano_pb_f32(&w, 5 + (uint32_t)param, n);
    return nano_frame_end(&w, NANO_MSG_CAB_SETTING);
}

size_t nano_build_cab_phase(uint8_t *out, size_t cap, bool inverted)
{
    nano_pb_writer_t w;
    nano_frame_begin(&w, out, cap);
    nano_pb_varint(&w, 8, inverted ? 1 : 0);
    return nano_frame_end(&w, NANO_MSG_CAB_SETTING);
}

size_t nano_build_cab_mic(uint8_t *out, size_t cap, uint32_t kind, const char *ir_name, uint8_t position, const char *mic)
{
    if (!ir_name || !ir_name[0] || !mic || !mic[0]) return 0;
    nano_pb_writer_t w;
    nano_frame_begin(&w, out, cap);
    size_t ir = nano_pb_begin(&w, 3);
    nano_pb_varint(&w, 1, kind);
    nano_pb_text(&w, 2, ir_name);
    nano_pb_varint(&w, 3, position); /* written even when 0 */
    nano_pb_text(&w, 4, mic);
    nano_pb_end(&w, ir); /* fails past 127 bytes */
    return nano_frame_end(&w, NANO_MSG_CAB_SETTING);
}

size_t nano_build_fx_model(uint8_t *out, size_t cap, uint8_t slot, uint32_t type)
{
    if (slot >= NANO_FX_SLOT_COUNT || !type) return 0;
    nano_pb_writer_t w;
    nano_frame_begin(&w, out, cap);
    nano_pb_varint(&w, 3, slot);
    nano_pb_varint(&w, 4, type);
    return nano_frame_end(&w, NANO_MSG_FX_MODEL);
}

size_t nano_build_fx_params_request(uint8_t *out, size_t cap, uint8_t slot)
{
    if (slot >= NANO_FX_SLOT_COUNT) return 0;
    nano_pb_writer_t w;
    nano_frame_begin(&w, out, cap);
    nano_pb_varint(&w, 1, OP_READ);
    nano_pb_varint(&w, 3, slot);
    return nano_frame_end(&w, NANO_MSG_FX_PARAMS_REQUEST);
}

size_t nano_build_fx_param(uint8_t *out, size_t cap, uint8_t slot, uint8_t param, float normalized)
{
    if (slot >= NANO_FX_SLOT_COUNT || param > 0x7F) return 0;
    float n = normalized < 0 ? 0 : normalized > 1 ? 1 : normalized;
    nano_pb_writer_t w;
    nano_frame_begin(&w, out, cap);
    nano_pb_varint(&w, 1, OP_UPDATE);
    nano_pb_varint(&w, 3, slot);
    nano_pb_varint(&w, 4, param);
    nano_pb_f32(&w, 5, n);
    return nano_frame_end(&w, NANO_MSG_FX_PARAM);
}
