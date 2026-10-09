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
 *   read      `08 C0 18 00 20 <slot - 1> 5F 00 00 00` (slot = state field 12) -> type 0x60, see
 *             nano_decode_cab_settings.
 *   level etc `09 C0 <field 5 / 6 / 7 = level / high pass / low pass, f32 0..1> 5E 00 00 00` (nano_scales.h)
 *   phase     `06 C0 40 <1 inverted / 0> 5E 00 00 00`
 *   mic       `<len> C0 1A <n> { 08 <kind> 12 <IR name> 18 <position 0..5> 22 <microphone> } 5E 00 00 00`:
 *             the factory IR is picked by name, position and microphone (both from the read).
 */
/* Returns 10, or 0 for slot 0 (state field 12: 1..5, 6 for most presets). */
size_t nano_build_cab_settings_request(uint8_t *out, size_t cap, uint8_t slot);
/* Returns 11 (the value is clamped to 0..1), or 0 for an unknown parameter. */
size_t nano_build_cab_setting(uint8_t *out, size_t cap, nano_cab_param_t param, float normalized);
/* Returns 8. */
size_t nano_build_cab_phase(uint8_t *out, size_t cap, bool inverted);
/* `kind` and `ir_name` as the read reported them (nano_cab_settings_t). Returns 0 when too long. */
size_t nano_build_cab_mic(uint8_t *out, size_t cap, uint32_t kind, const char *ir_name, uint8_t position, const char *mic);

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
