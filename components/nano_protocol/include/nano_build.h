/*
 * The c304 request frames: fixed requests and builders, byte-exact against captures and verified on the pedal.
 * Port of NanoGig `src/protocol/frames.ts` plus our own captures. Each builder writes into `out` and returns the
 * frame length, or 0 when the arguments are out of range or `cap` is too small (NANO_FRAME_MAX always fits).
 */
#ifndef NANO_BUILD_H
#define NANO_BUILD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "nano_frame.h"
#include "nano_scales.h"

#ifdef __cplusplus
extern "C" {
#endif

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
 * Returns NANO_PRESET_SELECT_LEN.
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
 * Raw 0..255 (nano_capture_volume_db); state field 44 carries it back. A live edit: it changes the sound, it
 * does not save the preset. Returns 12 or 13.
 */
size_t nano_build_capture_volume(uint8_t *out, size_t cap, uint8_t raw);

/*
 * Rename a preset (the pedal's RenamePreset, frame from DrD85/nano-cortex-controller, verified on the
 * user's pedal 2026-10-08): `<len> C0 08 01 18 <preset> 22 <n> <name> 6F 00 00 00`. The pedal stores the
 * name at once (no save needed) and answers type 0x70 `08 06 18 <preset> 20 <1 = ok>`. The pedal itself
 * checks nothing (2026-10-08: it took "", 3 characters, duplicates, spaces at the ends, 32 characters,
 * UTF-8), so the app keeps DrD85's rules: 4 to 32 characters, no duplicates; 31 here = the name cache.
 * Returns 0 for an empty name or one longer than NANO_PRESET_NAME_MAX.
 */
size_t nano_build_preset_rename(uint8_t *out, size_t cap, uint8_t preset_index, const char *name);

/*
 * IR (cab) of the current preset: Cortex Cloud's IR loader. Read and level / filter writes from
 * DrD85/nano-cortex-controller (MIT); phase, microphone and position writes from an Android HCI snoop of Cortex
 * Cloud (2026-10-09). All writes are live edits: audible, the preset is not saved.
 *   select    `08 C0 18 03 20 <slot> 1C 00 00 00`: slot 1..5 on the pedal's IR list, 0 = IR off (DrD85's
 *             nano_cab_select; the pedal's encoder reports the same 0x1C). The next state dump shows it.
 *   read      `08 C0 18 00 20 <slot - 1> 5F 00 00 00` (slot = state field 12) -> type 0x60, see
 *             nano_decode_cab_settings.
 *   level etc `09 C0 <field 5 / 6 / 7 = level / high pass / low pass, f32 0..1> 5E 00 00 00` (nano_scales.h)
 *   phase     `06 C0 40 <1 inverted / 0> 5E 00 00 00`
 *   mic       `<len> C0 1A <n> { 08 <kind> 12 <IR name> 18 <position 0..5> 22 <microphone> } 5E 00 00 00`:
 *             the factory IR is picked by name, position and microphone (both from the read).
 */
/* Returns 10. */
size_t nano_build_cab_select(uint8_t *out, size_t cap, uint8_t slot);
/* Returns 10, or 0 for slot 0 (state field 12: 1..5, 6 for most presets). */
size_t nano_build_cab_settings_request(uint8_t *out, size_t cap, uint8_t slot);
/* Returns 11 (the value is clamped to 0..1), or 0 for an unknown parameter. */
size_t nano_build_cab_setting(uint8_t *out, size_t cap, nano_cab_param_t param, float normalized);
/* Returns 8. */
size_t nano_build_cab_phase(uint8_t *out, size_t cap, bool inverted);
/* `kind` and `ir_name` as the read reported them (nano_cab_settings_t). Returns 0 when too long. */
size_t nano_build_cab_mic(uint8_t *out, size_t cap, uint32_t kind, const char *ir_name, uint8_t position, const char *mic);
/*
 * Load an IR from the pedal's library (nano_decode_library) into the current preset, a live edit like picking one
 * in Cortex Cloud's IR loader. The read's `kind` is the IR's index in its list (2026-10-09: user IR "Tay816 M251
 * Pz1" = kind 7 = 8th user IR).
 *   factory  `<len> C0 1A <n> { 08 <index> 12 <name> } 5E 00 00 00`: the microphone frame without position and
 *            microphone (the pedal's defaults)
 *   user     `<len> C0 22 <n> { 08 <index> 12 <name> } 5E 00 00 00`: guessed from the read (field 5 factory / 6 user)
 * `list` = NANO_IR_FACTORY / NANO_IR_USER. Returns 0 for an empty name or one that does not fit.
 */
size_t nano_build_cab_load(uint8_t *out, size_t cap, int list, uint32_t index, const char *name);

/*
 * Library request (Cortex Cloud's, byte-exact from the 2026-10-09 snoop; DrD85's RetrieveLibraryContent):
 * `0C C0 18 01 20 01 28 01 30 01 4C 00 00 00` asks for factory captures, factory IRs, user captures and user IRs
 * (fields 3..6, as the reply's), ~11 KB. `what` = NANO_LIB_CAPTURES and / or NANO_LIB_IRS (nano_decode.h): the IR
 * names alone are ~1.1 KB, the captures ~10 KB (each with a 64 character hash).
 */
size_t nano_build_library_request(uint8_t *out, size_t cap, int what);

/*
 * Captures and IRs from the library, as Cortex Cloud's loaders do them (Android HCI snoops, 2026-10-09 / -10). A tap
 * on an item previews it: a live edit of the preset (its unsaved flag goes on; the screen's lists stop there). "Use"
 * then writes the item into the bank slot the preset points at (the pedal's 25 captures / 5 IRs: every preset on that
 * slot hears it) and selects that slot; for an IR, the preview load follows. The slot loads are kept here, unused. `list` = NANO_IR_FACTORY / NANO_IR_USER, `index` its place there.
 *   capture preview  `<len> C0 <1A factory / 22 user> <n> { 08 <index> 12 <name> } 40 01 96 00 00 00`
 *                    -> type 0x97 `08 06 18 01 22 { 12 <name> 1A <hash> }`
 *   capture use      `<len> C0 18 <slot 0..24> <22 factory / 2A user> <n> { 08 <index> 12 <name> } 50 00 00 00`
 *                    -> type 0x51 `08 06 18 01`, then nano_build_capture_select(slot + 1)
 *   IR use           `<len> C0 18 <slot 0..4> <22 factory / 2A user> <n> { ... } 4E 00 00 00` -> type 0x4F, then
 *                    nano_build_cab_select(slot + 1) and nano_build_cab_load (the factory field is DrD85's)
 * Return 0 for an empty name, a slot out of range or one that does not fit.
 */
size_t nano_build_capture_preview(uint8_t *out, size_t cap, int list, uint32_t index, const char *name);
size_t nano_build_capture_slot_load(uint8_t *out, size_t cap, uint8_t slot, int list, uint32_t index, const char *name);
size_t nano_build_cab_slot_load(uint8_t *out, size_t cap, uint8_t slot, int list, uint32_t index, const char *name);
/* The capture in bank slot 1..25 on (Cortex Cloud after a use: `08 C0 18 01 20 <slot> 1C 00 00 00`), 0 = bypassed. */
size_t nano_build_capture_select(uint8_t *out, size_t cap, uint8_t slot);

/*
 * Gate threshold, raw 0..255 (Cortex Cloud's gate slider, 2026-10-10 snoop): `<len> C0 18 0B 20 <raw varint> 28 00
 * 1A 00 00 00`, the capture volume's frame with selector 11. State field 53 carries it back as f32 raw / 255
 * (verified 2026-10-10: 199 written, 0.78 read; Cortex Cloud shows it as 0..100 %). A live edit.
 */
size_t nano_build_gate_threshold(uint8_t *out, size_t cap, uint8_t raw);

/*
 * An FX block's model and parameters (Cortex Cloud's FX editor), frames from DrD85/nano-cortex-controller (MIT). Live
 * edits: audible, the preset is not saved. `slot` 0..4 = pre1..post3.
 *   model    `<len> C0 18 <slot> 20 <type varint> 88 00 00 00` (nano_fx_params.h: the models the slot takes)
 *   read     `08 C0 08 03 18 <slot> 89 00 00 00` -> type 0x8A (nano_decode_fx_params). Only for a block that is ON:
 *            DrD85 saw the pedal crash on a bypassed one.
 *   param    `0F C0 08 01 18 <slot> 20 <param> 2D <f32 0..1> 63 00 00 00` (verified 2026-10-08: the value changes)
 */
size_t nano_build_fx_model(uint8_t *out, size_t cap, uint8_t slot, uint32_t type);
size_t nano_build_fx_params_request(uint8_t *out, size_t cap, uint8_t slot);
/* The value is clamped to 0..1. */
size_t nano_build_fx_param(uint8_t *out, size_t cap, uint8_t slot, uint8_t param, float normalized);

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
