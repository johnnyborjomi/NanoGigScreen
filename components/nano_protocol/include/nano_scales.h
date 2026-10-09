/*
 * The pedal's raw values and what Cortex Cloud shows for them (dB, Hz), fitted to its readouts. Kept apart from
 * the frames: the screen shows these, the frames carry the raw values.
 */
#ifndef NANO_SCALES_H
#define NANO_SCALES_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Capture volume, raw 0..255 = -24..+12 dB on a curve, fitted to five readings (2026-10-07: 0 = -24, 39 = -12.0,
 * 102 = -2.9, 128 = 0.0, 255 = +12 dB): raw = 255 * ((dB + 24) / 36)^1.708, each within 0.05 dB.
 */
float nano_capture_volume_db(uint8_t raw);
uint8_t nano_capture_volume_raw(float db);
/* What Cortex Cloud shows for `raw`, in tenths of a dB: the value cut toward zero, not rounded
 * (raw 101 = -3.07 dB reads "-3.0" there; all five readings above agree), with 0.01 dB of slack
 * for the fit at the boundaries (raw 110 = -1.995 reads -2.0, not -1.9). */
int nano_capture_volume_tenths(uint8_t raw);

/*
 * IR (cab) settings, the pedal's 0..1 (Cortex Cloud's readouts, 2026-10-08/09): Level -96 + 108 * n^(1/3.5) dB
 * (0 dB at 0.66212); High pass 20 + 780 * n^(5/3) Hz; Low pass 1000 + 19000 * n^(5/3) Hz.
 */
typedef enum { NANO_CAB_LEVEL = 0, NANO_CAB_HIGH_PASS, NANO_CAB_LOW_PASS, NANO_CAB_PARAMS } nano_cab_param_t;
#define NANO_CAB_LEVEL_MIN_DB (-96.0f)
#define NANO_CAB_LEVEL_MAX_DB 12.0f
#define NANO_CAB_HIGH_PASS_MIN_HZ 20.0f
#define NANO_CAB_HIGH_PASS_MAX_HZ 800.0f
#define NANO_CAB_LOW_PASS_MIN_HZ 1000.0f
#define NANO_CAB_LOW_PASS_MAX_HZ 20000.0f
#define NANO_CAB_POSITIONS 6 /* microphone position: Cortex Cloud's 1..6, from the cone's centre to its edge (0..5 on the wire) */
/* The pedal's 0..1 as dB (Level) or Hz (filters), and back (clamped). */
float nano_cab_value(nano_cab_param_t param, float normalized);
float nano_cab_normalized(nano_cab_param_t param, float value);

#ifdef __cplusplus
}
#endif
#endif
