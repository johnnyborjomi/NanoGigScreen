/* Generated from DrD85/nano-cortex-controller main/fx_models.c (MIT, Dominik Schmidt), which exports the Nano Cortex
 * Editor's model data; regenerate rather than edit. See nano_fx_params.h. */
#include "nano_fx_params.h"

#include <stddef.h>

static const nano_fx_param_t m18_params[] = {
    { "Gain", NANO_FX_PARAM_RANGE, 0.0f, 10.0f, 0.1f, "", 1, 0, NULL },
    { "Tone", NANO_FX_PARAM_RANGE, 0.0f, 10.0f, 0.1f, "", 1, 0, NULL },
    { "Volume", NANO_FX_PARAM_RANGE, 0.0f, 10.0f, 0.1f, "", 1, 0, NULL },
};
static const nano_fx_param_t m13_params[] = {
    { "Gain", NANO_FX_PARAM_RANGE, 0.0f, 10.0f, 0.1f, "", 1, 0, NULL },
    { "Level", NANO_FX_PARAM_RANGE, 0.0f, 100.0f, 0.1f, "%", 1, 0, NULL },
};
static const nano_fx_param_t m6_params[] = {
    { "Gain", NANO_FX_PARAM_RANGE, 0.0f, 10.0f, 0.1f, "", 1, 0, NULL },
    { "Bass", NANO_FX_PARAM_RANGE, 0.0f, 10.0f, 0.1f, "", 1, 0, NULL },
    { "Treble", NANO_FX_PARAM_RANGE, 0.0f, 10.0f, 0.1f, "", 1, 0, NULL },
    { "Volume", NANO_FX_PARAM_RANGE, 0.0f, 10.0f, 0.1f, "", 1, 0, NULL },
};
static const nano_fx_param_t m3007_params[] = {
    { "Gain", NANO_FX_PARAM_RANGE, 0.0f, 10.0f, 0.1f, "", 1, 0, NULL },
    { "Bass", NANO_FX_PARAM_RANGE, 0.0f, 10.0f, 0.1f, "", 1, 0, NULL },
    { "Treble", NANO_FX_PARAM_RANGE, 0.0f, 10.0f, 0.1f, "", 1, 0, NULL },
    { "Volume", NANO_FX_PARAM_RANGE, 0.0f, 10.0f, 0.1f, "", 1, 0, NULL },
};
static const nano_fx_param_t m23_params[] = {
    { "Gain", NANO_FX_PARAM_RANGE, 0.0f, 10.0f, 0.1f, "", 1, 0, NULL },
    { "Bass", NANO_FX_PARAM_RANGE, 0.0f, 10.0f, 0.1f, "", 1, 0, NULL },
    { "Treble", NANO_FX_PARAM_RANGE, 0.0f, 10.0f, 0.1f, "", 1, 0, NULL },
    { "Volume", NANO_FX_PARAM_RANGE, 0.0f, 10.0f, 0.1f, "", 1, 0, NULL },
};
static const nano_fx_param_t m22_params[] = {
    { "Fuzz", NANO_FX_PARAM_RANGE, 0.0f, 10.0f, 0.1f, "", 1, 0, NULL },
    { "Volume", NANO_FX_PARAM_RANGE, 0.0f, 10.0f, 0.1f, "", 1, 0, NULL },
    { "Pickup", NANO_FX_PARAM_ENUM, 0, 0, 0, "", 0, 2, "HB\nSingle" },
    { "Pickup Level", NANO_FX_PARAM_RANGE, 0.0f, 10.0f, 0.1f, "", 1, 0, NULL },
};
static const nano_fx_param_t m27_params[] = {
    { "Overdrive", NANO_FX_PARAM_RANGE, 0.0f, 10.0f, 0.1f, "", 1, 0, NULL },
    { "Tone", NANO_FX_PARAM_RANGE, 0.0f, 10.0f, 0.1f, "", 1, 0, NULL },
    { "Level", NANO_FX_PARAM_RANGE, 0.0f, 100.0f, 0.1f, "%", 1, 0, NULL },
};
static const nano_fx_param_t m3000_params[] = {
    { "Drive", NANO_FX_PARAM_RANGE, 0.0f, 10.0f, 0.1f, "", 1, 0, NULL },
    { "Growl", NANO_FX_PARAM_ENUM, 0, 0, 0, "", 0, 2, "Off\nOn" },
    { "Midboost", NANO_FX_PARAM_ENUM, 0, 0, 0, "", 0, 2, "Off\nOn" },
    { "Tone", NANO_FX_PARAM_RANGE, 0.0f, 10.0f, 0.1f, "", 1, 0, NULL },
    { "Level", NANO_FX_PARAM_RANGE, 0.0f, 100.0f, 0.1f, "%", 1, 0, NULL },
    { "Blend", NANO_FX_PARAM_RANGE, 0.0f, 10.0f, 0.1f, "", 1, 0, NULL },
};
static const nano_fx_param_t m3_params[] = {
    { "Gain", NANO_FX_PARAM_RANGE, 0.0f, 10.0f, 0.1f, "", 1, 0, NULL },
    { "Volume", NANO_FX_PARAM_RANGE, 0.0f, 10.0f, 0.1f, "", 1, 0, NULL },
};
static const nano_fx_param_t m2_params[] = {
    { "Drive", NANO_FX_PARAM_RANGE, 0.0f, 10.0f, 0.1f, "", 1, 0, NULL },
    { "Peak", NANO_FX_PARAM_ENUM, 0, 0, 0, "", 0, 2, "LP\nHP" },
    { "Tone", NANO_FX_PARAM_RANGE, 0.0f, 10.0f, 0.1f, "", 1, 0, NULL },
    { "Volume", NANO_FX_PARAM_RANGE, 0.0f, 10.0f, 0.1f, "", 1, 0, NULL },
};
static const nano_fx_param_t m4_params[] = {
    { "Distortion", NANO_FX_PARAM_RANGE, 0.0f, 10.0f, 0.1f, "", 1, 0, NULL },
    { "Filter", NANO_FX_PARAM_RANGE, 0.0f, 10.0f, 0.1f, "", 1, 0, NULL },
    { "Volume", NANO_FX_PARAM_RANGE, 0.0f, 10.0f, 0.1f, "", 1, 0, NULL },
};
static const nano_fx_param_t m16001_params[] = {
    { "Noise Reduction", NANO_FX_PARAM_RANGE, 0.0f, 100.0f, 0.1f, "%", 1, 0, NULL },
};
static const nano_fx_param_t m4005_params[] = {
    { "65Hz", NANO_FX_PARAM_RANGE, -12.0f, 12.0f, 0.1f, "dB", 1, 0, NULL },
    { "125Hz", NANO_FX_PARAM_RANGE, -12.0f, 12.0f, 0.1f, "dB", 1, 0, NULL },
    { "250Hz", NANO_FX_PARAM_RANGE, -12.0f, 12.0f, 0.1f, "dB", 1, 0, NULL },
    { "500hz", NANO_FX_PARAM_RANGE, -12.0f, 12.0f, 0.1f, "dB", 1, 0, NULL },
    { "1kHz", NANO_FX_PARAM_RANGE, -12.0f, 12.0f, 0.1f, "dB", 1, 0, NULL },
    { "2kHz", NANO_FX_PARAM_RANGE, -12.0f, 12.0f, 0.1f, "dB", 1, 0, NULL },
    { "4kHz", NANO_FX_PARAM_RANGE, -12.0f, 12.0f, 0.1f, "dB", 1, 0, NULL },
    { "8kHz", NANO_FX_PARAM_RANGE, -12.0f, 12.0f, 0.1f, "dB", 1, 0, NULL },
    { "16kHz", NANO_FX_PARAM_RANGE, -12.0f, 12.0f, 0.1f, "dB", 1, 0, NULL },
    { "HPF", NANO_FX_PARAM_RANGE, 20.0f, 800.0f, 1.0f, "Hz", 0, 0, NULL },
    { "LPF", NANO_FX_PARAM_RANGE, 1000.0f, 16000.0f, 1.0f, "Hz", 0, 0, NULL },
    { "Output", NANO_FX_PARAM_RANGE, -12.0f, 12.0f, 0.1f, "dB", 1, 0, NULL },
};
static const nano_fx_param_t m4003_params[] = {
    { "HPF Slope", NANO_FX_PARAM_ENUM, 0, 0, 0, "", 0, 9, "Flat\n-6\n-12\n-18\n-24\n-30\n-36\n-42\n-48" },
    { "HPF Freq", NANO_FX_PARAM_RANGE, 20.0f, 20000.0f, 1.0f, "Hz", 0, 0, NULL },
    { "LPF Slope", NANO_FX_PARAM_ENUM, 0, 0, 0, "", 0, 9, "Flat\n-6\n-12\n-18\n-24\n-30\n-36\n-42\n-48" },
    { "LPF Freq", NANO_FX_PARAM_RANGE, 20.0f, 20000.0f, 1.0f, "Hz", 0, 0, NULL },
    { "Output", NANO_FX_PARAM_RANGE, -12.0f, 12.0f, 0.1f, "dB", 1, 0, NULL },
};
static const nano_fx_param_t m4001_params[] = {
    { "1 Gain", NANO_FX_PARAM_RANGE, -12.0f, 12.0f, 0.1f, "dB", 1, 0, NULL },
    { "1 Freq", NANO_FX_PARAM_RANGE, 0.1f, 10.0f, 0.1f, "", 1, 0, NULL },
    { "1 Q", NANO_FX_PARAM_RANGE, 0.1f, 10.0f, 0.1f, "", 1, 0, NULL },
    { "1 Type", NANO_FX_PARAM_ENUM, 0, 0, 0, "", 0, 5, "Peak\nHi Pass\nLo Pass\nHi Shelf\nLo Shelf" },
    { "1 Active", NANO_FX_PARAM_ENUM, 0, 0, 0, "", 0, 2, "Off\nOn" },
    { "2 Gain", NANO_FX_PARAM_RANGE, -12.0f, 12.0f, 0.1f, "dB", 1, 0, NULL },
    { "2 Freq", NANO_FX_PARAM_RANGE, 0.1f, 10.0f, 0.1f, "", 1, 0, NULL },
    { "2 Q", NANO_FX_PARAM_RANGE, 0.1f, 10.0f, 0.1f, "", 1, 0, NULL },
    { "2 Type", NANO_FX_PARAM_ENUM, 0, 0, 0, "", 0, 5, "Peak\nHi Pass\nLo Pass\nHi Shelf\nLo Shelf" },
    { "2 Active", NANO_FX_PARAM_ENUM, 0, 0, 0, "", 0, 2, "Off\nOn" },
    { "3 Gain", NANO_FX_PARAM_RANGE, -12.0f, 12.0f, 0.1f, "dB", 1, 0, NULL },
    { "3 Freq", NANO_FX_PARAM_RANGE, 0.1f, 10.0f, 0.1f, "", 1, 0, NULL },
    { "3 Q", NANO_FX_PARAM_RANGE, 0.1f, 10.0f, 0.1f, "", 1, 0, NULL },
    { "3 Type", NANO_FX_PARAM_ENUM, 0, 0, 0, "", 0, 5, "Peak\nHi Pass\nLo Pass\nHi Shelf\nLo Shelf" },
    { "3 Active", NANO_FX_PARAM_ENUM, 0, 0, 0, "", 0, 2, "Off\nOn" },
    { "Output", NANO_FX_PARAM_RANGE, -12.0f, 12.0f, 0.1f, "dB", 1, 0, NULL },
};
static const nano_fx_param_t m16002_params[] = {
    { "Threshold", NANO_FX_PARAM_RANGE, -90.0f, 0.0f, 0.1f, "dB", 1, 0, NULL },
    { "Attack", NANO_FX_PARAM_RANGE, 1.0f, 1000.0f, 1.0f, "ms", 0, 0, NULL },
    { "Hold", NANO_FX_PARAM_RANGE, 1.0f, 2000.0f, 1.0f, "ms", 0, 0, NULL },
    { "Release", NANO_FX_PARAM_RANGE, 2.0f, 5000.0f, 1.0f, "ms", 0, 0, NULL },
    { "Range", NANO_FX_PARAM_RANGE, -90.0f, -6.0f, 0.1f, "dB", 1, 0, NULL },
};
static const nano_fx_param_t m16006_params[] = {
    { "Level", NANO_FX_PARAM_RANGE, 0.0f, 100.0f, 0.1f, "%", 1, 0, NULL },
    { "Curve", NANO_FX_PARAM_ENUM, 0, 0, 0, "", 0, 2, "Linear\nLog" },
};
static const nano_fx_param_t m9012_params[] = {
    { "Wah", NANO_FX_PARAM_RANGE, 0.0f, 1.0f, 0.01f, "", 2, 0, NULL },
};
static const nano_fx_param_t m9010_params[] = {
    { "Wah", NANO_FX_PARAM_RANGE, 0.0f, 1.0f, 0.01f, "", 2, 0, NULL },
};
static const nano_fx_param_t m9014_params[] = {
    { "Wah", NANO_FX_PARAM_RANGE, 0.0f, 1.0f, 0.01f, "", 2, 0, NULL },
};
static const nano_fx_param_t m9013_params[] = {
    { "Wah", NANO_FX_PARAM_RANGE, 0.0f, 1.0f, 0.01f, "", 2, 0, NULL },
};
static const nano_fx_param_t m24006_params[] = {
    { "Sens", NANO_FX_PARAM_RANGE, 0.0f, 10.0f, 0.1f, "", 1, 0, NULL },
    { "Attack", NANO_FX_PARAM_RANGE, 1.0f, 1000.0f, 1.0f, "ms", 0, 0, NULL },
    { "Decay", NANO_FX_PARAM_RANGE, 0.0f, 10.0f, 0.1f, "", 1, 0, NULL },
    { "LP/BP/HP", NANO_FX_PARAM_RANGE, -1.0f, 1.0f, 0.01f, "", 2, 0, NULL },
    { "Level", NANO_FX_PARAM_RANGE, 0.0f, 100.0f, 0.1f, "%", 1, 0, NULL },
    { "Freq", NANO_FX_PARAM_RANGE, 20.0f, 20000.0f, 1.0f, "Hz", 0, 0, NULL },
    { "Freq Depth", NANO_FX_PARAM_RANGE, 0.0f, 100.0f, 0.1f, "%", 1, 0, NULL },
    { "Reso", NANO_FX_PARAM_RANGE, 1.0f, 10.0f, 0.01f, "", 2, 0, NULL },
    { "Mix", NANO_FX_PARAM_RANGE, 0.0f, 100.0f, 0.1f, "%", 1, 0, NULL },
};
static const nano_fx_param_t m24001_params[] = {
    { "Sensitivity", NANO_FX_PARAM_RANGE, 0.0f, 10.0f, 0.1f, "", 1, 0, NULL },
    { "Attack", NANO_FX_PARAM_RANGE, 1.0f, 1000.0f, 1.0f, "ms", 0, 0, NULL },
    { "Decay", NANO_FX_PARAM_RANGE, 0.0f, 10.0f, 0.1f, "", 1, 0, NULL },
    { "Color", NANO_FX_PARAM_RANGE, 0.0f, 10.0f, 0.1f, "", 1, 0, NULL },
    { "Intensity", NANO_FX_PARAM_RANGE, 0.0f, 10.0f, 0.1f, "", 1, 0, NULL },
    { "Blend", NANO_FX_PARAM_RANGE, 0.0f, 10.0f, 0.1f, "", 1, 0, NULL },
    { "Trig Detection", NANO_FX_PARAM_ENUM, 0, 0, 0, "", 0, 2, "Down\nUp" },
    { "Trigger Mode", NANO_FX_PARAM_ENUM, 0, 0, 0, "", 0, 3, "Off\nFull\n1/2" },
    { "Filter Cutoff", NANO_FX_PARAM_ENUM, 0, 0, 0, "", 0, 4, "Low\nA\nB\nHigh" },
    { "Filter Type", NANO_FX_PARAM_ENUM, 0, 0, 0, "", 0, 3, "Lowpass\nBandpass\nHighpass" },
    { "Level", NANO_FX_PARAM_RANGE, 0.0f, 100.0f, 0.1f, "%", 1, 0, NULL },
};
static const nano_fx_param_t m5001_params[] = {
    { "Input", NANO_FX_PARAM_RANGE, -48.0f, 0.0f, 0.1f, "dB", 1, 0, NULL },
    { "Ratio", NANO_FX_PARAM_ENUM, 0, 0, 0, "", 0, 5, "4\n8\n12\n20\nAll" },
    { "Attack", NANO_FX_PARAM_RANGE, 0.02f, 0.8f, 0.01f, "ms", 2, 0, NULL },
    { "Release", NANO_FX_PARAM_RANGE, 0.06f, 1.1f, 0.01f, "s", 2, 0, NULL },
    { "Makeup", NANO_FX_PARAM_RANGE, -48.0f, 48.0f, 0.01f, "dB", 2, 0, NULL },
    { "Mix", NANO_FX_PARAM_RANGE, 0.0f, 100.0f, 0.1f, "%", 1, 0, NULL },
};
static const nano_fx_param_t m5007_params[] = {
    { "Threshold", NANO_FX_PARAM_RANGE, -60.0f, 12.0f, 0.1f, "dB", 1, 0, NULL },
    { "Ratio", NANO_FX_PARAM_RANGE, 2.0f, 20.0f, 0.01f, "", 2, 0, NULL },
    { "Attack", NANO_FX_PARAM_RANGE, 1.0f, 250.0f, 0.01f, "ms", 2, 0, NULL },
    { "Release", NANO_FX_PARAM_RANGE, 50.0f, 1200.0f, 1.0f, "ms", 0, 0, NULL },
    { "Makeup", NANO_FX_PARAM_RANGE, -48.0f, 48.0f, 0.01f, "dB", 2, 0, NULL },
    { "Mix", NANO_FX_PARAM_RANGE, 0.0f, 100.0f, 0.1f, "%", 1, 0, NULL },
};
static const nano_fx_param_t m5004_params[] = {
    { "Threshold", NANO_FX_PARAM_RANGE, -60.0f, 12.0f, 0.1f, "dB", 1, 0, NULL },
    { "Ratio", NANO_FX_PARAM_ENUM, 0, 0, 0, "", 0, 3, "2\n4\n10" },
    { "Attack", NANO_FX_PARAM_RANGE, 0.1f, 30.0f, 0.01f, "ms", 2, 0, NULL },
    { "Release", NANO_FX_PARAM_RANGE, 0.01f, 1.2f, 0.01f, "s", 2, 0, NULL },
    { "Makeup", NANO_FX_PARAM_RANGE, -48.0f, 48.0f, 0.01f, "dB", 2, 0, NULL },
    { "Mix", NANO_FX_PARAM_RANGE, 0.0f, 100.0f, 0.1f, "%", 1, 0, NULL },
};
static const nano_fx_param_t m5005_params[] = {
    { "Threshold", NANO_FX_PARAM_RANGE, -60.0f, 12.0f, 0.1f, "dB", 1, 0, NULL },
    { "Ratio", NANO_FX_PARAM_RANGE, 2.0f, 10.0f, 0.01f, "", 2, 0, NULL },
    { "Attack", NANO_FX_PARAM_RANGE, 1.0f, 250.0f, 0.01f, "ms", 2, 0, NULL },
    { "Release", NANO_FX_PARAM_RANGE, 50.0f, 1200.0f, 1.0f, "ms", 0, 0, NULL },
    { "Makeup", NANO_FX_PARAM_RANGE, -48.0f, 48.0f, 0.01f, "dB", 2, 0, NULL },
    { "Mix", NANO_FX_PARAM_RANGE, 0.0f, 100.0f, 0.1f, "%", 1, 0, NULL },
};
static const nano_fx_param_t m18001_params[] = {
    { "Mix", NANO_FX_PARAM_RANGE, 0.0f, 100.0f, 0.1f, "%", 1, 0, NULL },
    { "Semitones", NANO_FX_PARAM_RANGE, -12.0f, 12.0f, 1.0f, "Sem", 0, 0, NULL },
    { "Pitch Fine", NANO_FX_PARAM_RANGE, -100.0f, 100.0f, 1.0f, "Cent", 0, 0, NULL },
    { "High Pass", NANO_FX_PARAM_RANGE, 20.0f, 2000.0f, 1.0f, "Hz", 0, 0, NULL },
    { "Low Pass", NANO_FX_PARAM_RANGE, 200.0f, 19900.0f, 1.0f, "Hz", 0, 0, NULL },
};
static const nano_fx_param_t m16011_params[] = {
    { "Spread", NANO_FX_PARAM_RANGE, 3.0f, 60.0f, 0.01f, "ms", 2, 0, NULL },
    { "Dry Level", NANO_FX_PARAM_RANGE, -40.0f, 12.0f, 0.1f, "dB", 1, 0, NULL },
    { "FX Level", NANO_FX_PARAM_RANGE, -40.0f, 12.0f, 0.1f, "dB", 1, 0, NULL },
};
static const nano_fx_param_t m5012_params[] = {
    { "Input", NANO_FX_PARAM_RANGE, -48.0f, 0.0f, 0.1f, "dB", 1, 0, NULL },
    { "Ratio", NANO_FX_PARAM_ENUM, 0, 0, 0, "", 0, 5, "4\n8\n12\n20\nAll" },
    { "Attack", NANO_FX_PARAM_RANGE, 0.02f, 0.8f, 0.01f, "ms", 2, 0, NULL },
    { "Release", NANO_FX_PARAM_RANGE, 0.06f, 1.1f, 0.01f, "s", 2, 0, NULL },
    { "Makeup", NANO_FX_PARAM_RANGE, -48.0f, 48.0f, 0.01f, "dB", 2, 0, NULL },
    { "Mix", NANO_FX_PARAM_RANGE, 0.0f, 100.0f, 0.1f, "%", 1, 0, NULL },
};
static const nano_fx_param_t m5015_params[] = {
    { "Threshold", NANO_FX_PARAM_RANGE, -60.0f, 12.0f, 0.1f, "dB", 1, 0, NULL },
    { "Ratio", NANO_FX_PARAM_RANGE, 2.0f, 20.0f, 0.01f, "", 2, 0, NULL },
    { "Attack", NANO_FX_PARAM_RANGE, 1.0f, 250.0f, 0.01f, "ms", 2, 0, NULL },
    { "Release", NANO_FX_PARAM_RANGE, 50.0f, 1200.0f, 1.0f, "ms", 0, 0, NULL },
    { "Makeup", NANO_FX_PARAM_RANGE, -48.0f, 48.0f, 0.01f, "dB", 2, 0, NULL },
    { "Mix", NANO_FX_PARAM_RANGE, 0.0f, 100.0f, 0.1f, "%", 1, 0, NULL },
};
static const nano_fx_param_t m5013_params[] = {
    { "Threshold", NANO_FX_PARAM_RANGE, -60.0f, 12.0f, 0.1f, "dB", 1, 0, NULL },
    { "Ratio", NANO_FX_PARAM_ENUM, 0, 0, 0, "", 0, 3, "2\n4\n10" },
    { "Attack", NANO_FX_PARAM_RANGE, 0.1f, 30.0f, 0.01f, "ms", 2, 0, NULL },
    { "Release", NANO_FX_PARAM_RANGE, 0.01f, 1.2f, 0.01f, "s", 2, 0, NULL },
    { "Makeup", NANO_FX_PARAM_RANGE, -48.0f, 48.0f, 0.01f, "dB", 2, 0, NULL },
    { "Mix", NANO_FX_PARAM_RANGE, 0.0f, 100.0f, 0.1f, "%", 1, 0, NULL },
};
static const nano_fx_param_t m5014_params[] = {
    { "Threshold", NANO_FX_PARAM_RANGE, -60.0f, 12.0f, 0.1f, "dB", 1, 0, NULL },
    { "Ratio", NANO_FX_PARAM_RANGE, 2.0f, 10.0f, 0.01f, "", 2, 0, NULL },
    { "Attack", NANO_FX_PARAM_RANGE, 1.0f, 250.0f, 0.01f, "ms", 2, 0, NULL },
    { "Release", NANO_FX_PARAM_RANGE, 50.0f, 1200.0f, 1.0f, "ms", 0, 0, NULL },
    { "Makeup", NANO_FX_PARAM_RANGE, -48.0f, 48.0f, 0.01f, "dB", 2, 0, NULL },
    { "Mix", NANO_FX_PARAM_RANGE, 0.0f, 100.0f, 0.1f, "%", 1, 0, NULL },
};
static const nano_fx_param_t m7024_params[] = {
    { "Mix", NANO_FX_PARAM_RANGE, 0.0f, 100.0f, 0.1f, "%", 1, 0, NULL },
    { "Rate", NANO_FX_PARAM_RANGE, 0.0f, 100.0f, 1.0f, "%", 0, 0, NULL },
    { "Depth", NANO_FX_PARAM_RANGE, 0.0f, 100.0f, 0.1f, "%", 1, 0, NULL },
    { "Type", NANO_FX_PARAM_ENUM, 0, 0, 0, "", 0, 3, "CE2-C\nCE1-C\nCE1-V" },
    { "Width", NANO_FX_PARAM_RANGE, 0.0f, 100.0f, 0.1f, "%", 1, 0, NULL },
    { "Output", NANO_FX_PARAM_RANGE, -12.0f, 12.0f, 0.1f, "dB", 1, 0, NULL },
    { "Sync", NANO_FX_PARAM_ENUM, 0, 0, 0, "", 0, 2, "Off\nOn" },
    { "Sync Note", NANO_FX_PARAM_ENUM, 0, 0, 0, "", 0, 14, "1/16\n1/8T\n1/16D\n1/8\n1/4T\n1/8D\n1/4\n1/2T\n1/4D\n1/2\n1/1T\n1/2D\n1/1\n1/1D" },
};
static const uint8_t m7024_order[] = { 0, 1, 6, 7, 2, 3, 4, 5 };
static const nano_fx_param_t m7027_params[] = {
    { "Mix", NANO_FX_PARAM_RANGE, 0.0f, 100.0f, 0.1f, "%", 1, 0, NULL },
    { "Mode", NANO_FX_PARAM_ENUM, 0, 0, 0, "", 0, 10, "1\n2\n3\n4\n1+2\n1+3\n1+4\n2+3\n2+4\n3+4" },
    { "Mode Type", NANO_FX_PARAM_ENUM, 0, 0, 0, "", 0, 2, "S\nSDD-320" },
    { "Drive", NANO_FX_PARAM_RANGE, 0.0f, 100.0f, 1.0f, "%", 0, 0, NULL },
    { "Output", NANO_FX_PARAM_RANGE, -12.0f, 12.0f, 0.1f, "dB", 1, 0, NULL },
};
static const nano_fx_param_t m7023_params[] = {
    { "Mix", NANO_FX_PARAM_RANGE, 0.0f, 100.0f, 0.1f, "%", 1, 0, NULL },
    { "Rate", NANO_FX_PARAM_RANGE, 0.2f, 3.0f, 0.01f, "Hz", 2, 0, NULL },
    { "Depth", NANO_FX_PARAM_RANGE, 0.0f, 100.0f, 0.1f, "%", 1, 0, NULL },
    { "Width", NANO_FX_PARAM_RANGE, 0.0f, 100.0f, 0.1f, "%", 1, 0, NULL },
    { "Output", NANO_FX_PARAM_RANGE, -12.0f, 12.0f, 0.1f, "dB", 1, 0, NULL },
    { "Sync", NANO_FX_PARAM_ENUM, 0, 0, 0, "", 0, 2, "Off\nOn" },
    { "Sync Note", NANO_FX_PARAM_ENUM, 0, 0, 0, "", 0, 11, "1/8\n1/4T\n1/8D\n1/4\n1/2T\n1/4D\n1/2\n1/1T\n1/2D\n1/1\n1/1D" },
};
static const uint8_t m7023_order[] = { 0, 1, 5, 6, 2, 3, 4 };
static const nano_fx_param_t m7022_params[] = {
    { "Mix", NANO_FX_PARAM_RANGE, 0.0f, 100.0f, 0.1f, "%", 1, 0, NULL },
    { "Speed", NANO_FX_PARAM_RANGE, 0.0f, 100.0f, 1.0f, "%", 0, 0, NULL },
    { "Depth", NANO_FX_PARAM_RANGE, 0.0f, 100.0f, 0.1f, "%", 1, 0, NULL },
    { "Mode", NANO_FX_PARAM_ENUM, 0, 0, 0, "", 0, 2, "CHO1\nCHO2" },
    { "Output", NANO_FX_PARAM_RANGE, -12.0f, 12.0f, 0.1f, "dB", 1, 0, NULL },
    { "Sync", NANO_FX_PARAM_ENUM, 0, 0, 0, "", 0, 2, "Off\nOn" },
    { "Sync Note", NANO_FX_PARAM_ENUM, 0, 0, 0, "", 0, 14, "1/16\n1/8T\n1/16D\n1/8\n1/4T\n1/8D\n1/4\n1/2T\n1/4D\n1/2\n1/1T\n1/2D\n1/1\n1/1D" },
};
static const uint8_t m7022_order[] = { 0, 1, 5, 6, 2, 3, 4 };
static const nano_fx_param_t m7021_params[] = {
    { "Mix", NANO_FX_PARAM_RANGE, 0.0f, 100.0f, 0.1f, "%", 1, 0, NULL },
    { "Manual", NANO_FX_PARAM_RANGE, 0.0f, 10.0f, 0.1f, "", 1, 0, NULL },
    { "Width", NANO_FX_PARAM_RANGE, 0.0f, 100.0f, 0.1f, "%", 1, 0, NULL },
    { "Speed", NANO_FX_PARAM_RANGE, 0.0f, 100.0f, 1.0f, "%", 0, 0, NULL },
    { "Regen", NANO_FX_PARAM_RANGE, 0.0f, 10.0f, 0.1f, "", 1, 0, NULL },
    { "Output", NANO_FX_PARAM_RANGE, -12.0f, 12.0f, 0.1f, "dB", 1, 0, NULL },
    { "Sync", NANO_FX_PARAM_ENUM, 0, 0, 0, "", 0, 2, "Off\nOn" },
    { "Sync Note", NANO_FX_PARAM_ENUM, 0, 0, 0, "", 0, 14, "1/16\n1/8T\n1/16D\n1/8\n1/4T\n1/8D\n1/4\n1/2T\n1/4D\n1/2\n1/1T\n1/2D\n1/1\n1/1D" },
};
static const uint8_t m7021_order[] = { 0, 1, 2, 3, 6, 7, 4, 5 };
static const nano_fx_param_t m7028_params[] = {
    { "Mix", NANO_FX_PARAM_RANGE, 0.0f, 100.0f, 0.1f, "%", 1, 0, NULL },
    { "Speed", NANO_FX_PARAM_RANGE, 0.0f, 100.0f, 1.0f, "%", 0, 0, NULL },
    { "Type", NANO_FX_PARAM_ENUM, 0, 0, 0, "", 0, 2, "90\n45" },
    { "Mode", NANO_FX_PARAM_ENUM, 0, 0, 0, "", 0, 2, "Block\nScript" },
    { "Output", NANO_FX_PARAM_RANGE, -12.0f, 12.0f, 0.1f, "dB", 1, 0, NULL },
    { "Sync", NANO_FX_PARAM_ENUM, 0, 0, 0, "", 0, 2, "Off\nOn" },
    { "Sync Note", NANO_FX_PARAM_ENUM, 0, 0, 0, "", 0, 17, "1/32\n1/16T\n1/32D\n1/16\n1/8T\n1/16D\n1/8\n1/4T\n1/8D\n1/4\n1/2T\n1/4D\n1/2\n1/1T\n1/2D\n1/1\n1/1D" },
};
static const uint8_t m7028_order[] = { 0, 1, 5, 6, 2, 3, 4 };
static const nano_fx_param_t m7029_params[] = {
    { "Mix", NANO_FX_PARAM_RANGE, 0.0f, 100.0f, 0.1f, "%", 1, 0, NULL },
    { "Vibe", NANO_FX_PARAM_ENUM, 0, 0, 0, "", 0, 2, "Off\nOn" },
    { "Speed", NANO_FX_PARAM_RANGE, 0.0f, 100.0f, 1.0f, "%", 0, 0, NULL },
    { "Level", NANO_FX_PARAM_RANGE, 0.0f, 100.0f, 0.1f, "%", 1, 0, NULL },
    { "Depth", NANO_FX_PARAM_RANGE, 0.0f, 100.0f, 0.1f, "%", 1, 0, NULL },
    { "Output", NANO_FX_PARAM_RANGE, -12.0f, 12.0f, 0.1f, "dB", 1, 0, NULL },
    { "Sync", NANO_FX_PARAM_ENUM, 0, 0, 0, "", 0, 2, "Off\nOn" },
    { "Sync Note", NANO_FX_PARAM_ENUM, 0, 0, 0, "", 0, 14, "1/16\n1/8T\n1/16D\n1/8\n1/4T\n1/8D\n1/4\n1/2T\n1/4D\n1/2\n1/1T\n1/2D\n1/1\n1/1D" },
};
static const uint8_t m7029_order[] = { 0, 1, 2, 6, 7, 3, 4, 5 };
static const nano_fx_param_t m7004_params[] = {
    { "Rate", NANO_FX_PARAM_RANGE, 0.1f, 20.0f, 0.1f, "Hz", 1, 0, NULL },
    { "Depth", NANO_FX_PARAM_RANGE, 0.0f, 100.0f, 0.1f, "%", 1, 0, NULL },
    { "Waveform", NANO_FX_PARAM_ENUM, 0, 0, 0, "", 0, 5, "Sine\nTriangle\nSquare\nSaw Up\nSaw Dn" },
    { "Duty Cycle", NANO_FX_PARAM_RANGE, 0.0f, 100.0f, 0.1f, "%", 1, 0, NULL },
    { "Width", NANO_FX_PARAM_RANGE, 0.0f, 100.0f, 0.1f, "%", 1, 0, NULL },
    { "Smoothing", NANO_FX_PARAM_RANGE, 0.0f, 100.0f, 0.1f, "%", 1, 0, NULL },
    { "LFO Active", NANO_FX_PARAM_ENUM, 0, 0, 0, "", 0, 2, "Off\nOn" },
    { "Fade In", NANO_FX_PARAM_RANGE, 1.0f, 5000.0f, 1.0f, "ms", 0, 0, NULL },
    { "Fade Out", NANO_FX_PARAM_RANGE, 1.0f, 5000.0f, 1.0f, "ms", 0, 0, NULL },
    { "Boost", NANO_FX_PARAM_RANGE, 0.0f, 100.0f, 0.1f, "%", 1, 0, NULL },
    { "Sync", NANO_FX_PARAM_ENUM, 0, 0, 0, "", 0, 2, "Off\nOn" },
    { "Sync Note", NANO_FX_PARAM_ENUM, 0, 0, 0, "", 0, 17, "1/32\n1/16T\n1/32D\n1/16\n1/8T\n1/16D\n1/8\n1/4T\n1/8D\n1/4\n1/2T\n1/4D\n1/2\n1/1T\n1/2D\n1/1\n1/1D" },
};
static const uint8_t m7004_order[] = { 0, 10, 11, 1, 2, 3, 4, 5, 6, 7, 8, 9 };
static const nano_fx_param_t m6010_params[] = {
    { "Mix", NANO_FX_PARAM_RANGE, 0.0f, 100.0f, 0.1f, "%", 1, 0, NULL },
    { "Feedback", NANO_FX_PARAM_RANGE, 0.0f, 100.0f, 0.1f, "%", 1, 0, NULL },
    { "High Pass", NANO_FX_PARAM_RANGE, 20.0f, 800.0f, 1.0f, "Hz", 0, 0, NULL },
    { "Low Pass", NANO_FX_PARAM_RANGE, 1000.0f, 16000.0f, 1.0f, "Hz", 0, 0, NULL },
    { "Ping Pong", NANO_FX_PARAM_ENUM, 0, 0, 0, "", 0, 2, "Off\nOn" },
    { "Delay Time", NANO_FX_PARAM_RANGE, 200.0f, 1100.0f, 0.1f, "ms", 1, 0, NULL },
    { "Mod Rate", NANO_FX_PARAM_RANGE, 0.1f, 10.0f, 0.1f, "Hz", 1, 0, NULL },
    { "Mod Depth", NANO_FX_PARAM_RANGE, 0.0f, 100.0f, 0.1f, "%", 1, 0, NULL },
    { "Width", NANO_FX_PARAM_RANGE, 0.0f, 100.0f, 0.1f, "%", 1, 0, NULL },
    { "Drive", NANO_FX_PARAM_RANGE, 0.0f, 10.0f, 0.1f, "", 1, 0, NULL },
    { "Sync", NANO_FX_PARAM_ENUM, 0, 0, 0, "", 0, 2, "Off\nOn" },
    { "Sync Note", NANO_FX_PARAM_ENUM, 0, 0, 0, "", 0, 14, "1/16\n1/8T\n1/16D\n1/8\n1/4T\n1/8D\n1/4\n1/2T\n1/4D\n1/2\n1/1T\n1/2D\n1/1\n1/1D" },
};
static const uint8_t m6010_order[] = { 0, 1, 2, 3, 4, 10, 11, 5, 6, 7, 8, 9 };
static const nano_fx_param_t m6015_params[] = {
    { "Mix", NANO_FX_PARAM_RANGE, 0.0f, 100.0f, 0.1f, "%", 1, 0, NULL },
    { "Tap Preset", NANO_FX_PARAM_ENUM, 0, 0, 0, "", 0, 9, "1\n2\n3\n4\n4 Alt\n5\n5 Alt\n6\n6 Alt" },
    { "Delay Time", NANO_FX_PARAM_RANGE, 27.0f, 2000.0f, 0.01f, "ms", 2, 0, NULL },
    { "Feedback", NANO_FX_PARAM_RANGE, 0.0f, 100.0f, 0.1f, "%", 1, 0, NULL },
    { "Diffusion", NANO_FX_PARAM_RANGE, 0.0f, 100.0f, 0.1f, "%", 1, 0, NULL },
    { "High Pass", NANO_FX_PARAM_RANGE, 20.0f, 800.0f, 1.0f, "Hz", 0, 0, NULL },
    { "Low Pass", NANO_FX_PARAM_RANGE, 1000.0f, 16000.0f, 1.0f, "Hz", 0, 0, NULL },
    { "Mod Rate", NANO_FX_PARAM_RANGE, 0.1f, 10.0f, 0.1f, "Hz", 1, 0, NULL },
    { "Mod Depth", NANO_FX_PARAM_RANGE, 0.0f, 100.0f, 0.1f, "%", 1, 0, NULL },
    { "Vintage Mode", NANO_FX_PARAM_ENUM, 0, 0, 0, "", 0, 2, "Off\nOn" },
    { "Sync", NANO_FX_PARAM_ENUM, 0, 0, 0, "", 0, 2, "Off\nOn" },
    { "Sync Note", NANO_FX_PARAM_ENUM, 0, 0, 0, "", 0, 21, "1/64T\n1/64\n1/32T\n1/64D\n1/32\n1/16T\n1/32D\n1/16\n1/8T\n1/16D\n1/8\n1/4T\n1/8D\n1/4\n1/2T\n1/4D\n1/2\n1/1T\n1/2D\n1/1\n1/1D" },
};
static const uint8_t m6015_order[] = { 0, 1, 10, 11, 2, 3, 4, 5, 6, 7, 8, 9 };
static const nano_fx_param_t m6011_params[] = {
    { "Mix", NANO_FX_PARAM_RANGE, 0.0f, 100.0f, 0.1f, "%", 1, 0, NULL },
    { "Feedback", NANO_FX_PARAM_RANGE, 0.0f, 100.0f, 0.1f, "%", 1, 0, NULL },
    { "High Pass", NANO_FX_PARAM_RANGE, 20.0f, 800.0f, 1.0f, "Hz", 0, 0, NULL },
    { "Low Pass", NANO_FX_PARAM_RANGE, 1000.0f, 16000.0f, 1.0f, "Hz", 0, 0, NULL },
    { "Ping Pong", NANO_FX_PARAM_ENUM, 0, 0, 0, "", 0, 2, "Off\nOn" },
    { "Delay Time", NANO_FX_PARAM_RANGE, 7.0f, 6000.0f, 1.0f, "ms", 0, 0, NULL },
    { "Mod Rate", NANO_FX_PARAM_RANGE, 0.1f, 10.0f, 0.1f, "Hz", 1, 0, NULL },
    { "Mod Depth", NANO_FX_PARAM_RANGE, 0.0f, 100.0f, 0.1f, "%", 1, 0, NULL },
    { "Width", NANO_FX_PARAM_RANGE, 0.0f, 100.0f, 0.1f, "%", 1, 0, NULL },
    { "Dyn Depth", NANO_FX_PARAM_RANGE, 0.0f, 100.0f, 0.1f, "%", 1, 0, NULL },
    { "Dyn Mode", NANO_FX_PARAM_ENUM, 0, 0, 0, "", 0, 3, "Off\nDuck\nGate" },
    { "Threshold", NANO_FX_PARAM_RANGE, -65.0f, -10.0f, 0.1f, "dB", 1, 0, NULL },
    { "Attack", NANO_FX_PARAM_RANGE, 1.0f, 2000.0f, 1.0f, "ms", 0, 0, NULL },
    { "Release", NANO_FX_PARAM_RANGE, 1.0f, 2000.0f, 1.0f, "ms", 0, 0, NULL },
    { "Knee", NANO_FX_PARAM_RANGE, 1.0f, 20.0f, 0.01f, "dB", 2, 0, NULL },
    { "Feedback Depth", NANO_FX_PARAM_RANGE, 0.0f, 100.0f, 0.1f, "%", 1, 0, NULL },
    { "Sync", NANO_FX_PARAM_ENUM, 0, 0, 0, "", 0, 2, "Off\nOn" },
    { "Sync Note", NANO_FX_PARAM_ENUM, 0, 0, 0, "", 0, 21, "1/64T\n1/64\n1/32T\n1/64D\n1/32\n1/16T\n1/32D\n1/16\n1/8T\n1/16D\n1/8\n1/4T\n1/8D\n1/4\n1/2T\n1/4D\n1/2\n1/1T\n1/2D\n1/1\n1/1D" },
};
static const uint8_t m6011_order[] = { 0, 1, 2, 3, 4, 16, 17, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };
static const nano_fx_param_t m6012_params[] = {
    { "Mix", NANO_FX_PARAM_RANGE, 0.0f, 100.0f, 0.1f, "%", 1, 0, NULL },
    { "Delay Time L", NANO_FX_PARAM_RANGE, 7.0f, 6000.0f, 1.0f, "ms", 0, 0, NULL },
    { "Feedback L", NANO_FX_PARAM_RANGE, 0.0f, 100.0f, 0.1f, "%", 1, 0, NULL },
    { "X-Feedback", NANO_FX_PARAM_RANGE, 0.0f, 100.0f, 0.1f, "%", 1, 0, NULL },
    { "Delay Time R", NANO_FX_PARAM_RANGE, 7.0f, 6000.0f, 1.0f, "ms", 0, 0, NULL },
    { "Feedback R", NANO_FX_PARAM_RANGE, 0.0f, 100.0f, 0.1f, "%", 1, 0, NULL },
    { "High Pass", NANO_FX_PARAM_RANGE, 20.0f, 800.0f, 1.0f, "Hz", 0, 0, NULL },
    { "Low Pass", NANO_FX_PARAM_RANGE, 1000.0f, 16000.0f, 1.0f, "Hz", 0, 0, NULL },
    { "Mod Rate", NANO_FX_PARAM_RANGE, 0.1f, 10.0f, 0.1f, "Hz", 1, 0, NULL },
    { "Mod Depth", NANO_FX_PARAM_RANGE, 0.0f, 100.0f, 0.1f, "%", 1, 0, NULL },
    { "Link FBack", NANO_FX_PARAM_ENUM, 0, 0, 0, "", 0, 2, "Off\nOn" },
    { "Dyn Depth", NANO_FX_PARAM_RANGE, 0.0f, 100.0f, 0.1f, "%", 1, 0, NULL },
    { "Dyn Mode", NANO_FX_PARAM_ENUM, 0, 0, 0, "", 0, 3, "Off\nDuck\nGate" },
    { "Threshold", NANO_FX_PARAM_RANGE, -65.0f, -10.0f, 0.1f, "dB", 1, 0, NULL },
    { "Attack", NANO_FX_PARAM_RANGE, 1.0f, 2000.0f, 1.0f, "ms", 0, 0, NULL },
    { "Release", NANO_FX_PARAM_RANGE, 1.0f, 2000.0f, 1.0f, "ms", 0, 0, NULL },
    { "Knee", NANO_FX_PARAM_RANGE, 1.0f, 20.0f, 0.01f, "dB", 2, 0, NULL },
    { "Feedback Depth", NANO_FX_PARAM_RANGE, 0.0f, 100.0f, 0.1f, "%", 1, 0, NULL },
    { "Sync L", NANO_FX_PARAM_ENUM, 0, 0, 0, "", 0, 2, "Off\nOn" },
    { "Sync Note L", NANO_FX_PARAM_ENUM, 0, 0, 0, "", 0, 21, "1/64T\n1/64\n1/32T\n1/64D\n1/32\n1/16T\n1/32D\n1/16\n1/8T\n1/16D\n1/8\n1/4T\n1/8D\n1/4\n1/2T\n1/4D\n1/2\n1/1T\n1/2D\n1/1\n1/1D" },
    { "Sync R", NANO_FX_PARAM_ENUM, 0, 0, 0, "", 0, 2, "Off\nOn" },
    { "Sync Note R", NANO_FX_PARAM_ENUM, 0, 0, 0, "", 0, 21, "1/64T\n1/64\n1/32T\n1/64D\n1/32\n1/16T\n1/32D\n1/16\n1/8T\n1/16D\n1/8\n1/4T\n1/8D\n1/4\n1/2T\n1/4D\n1/2\n1/1T\n1/2D\n1/1\n1/1D" },
};
static const uint8_t m6012_order[] = { 0, 18, 19, 1, 2, 3, 20, 21, 4, 5, 10, 11, 12, 13, 14, 15, 16, 17, 6, 7, 8, 9 };
static const nano_fx_param_t m6014_params[] = {
    { "Mix", NANO_FX_PARAM_RANGE, 0.0f, 100.0f, 0.1f, "%", 1, 0, NULL },
    { "Delay Time L", NANO_FX_PARAM_RANGE, 20.0f, 4000.0f, 1.0f, "ms", 0, 0, NULL },
    { "Feedback L", NANO_FX_PARAM_RANGE, 0.0f, 100.0f, 0.1f, "%", 1, 0, NULL },
    { "X-Feedback", NANO_FX_PARAM_RANGE, 0.0f, 100.0f, 0.1f, "%", 1, 0, NULL },
    { "Feedback Mode", NANO_FX_PARAM_ENUM, 0, 0, 0, "", 0, 2, "Reverse\nFlip" },
    { "Delay Time R", NANO_FX_PARAM_RANGE, 20.0f, 4000.0f, 1.0f, "ms", 0, 0, NULL },
    { "Feedback R", NANO_FX_PARAM_RANGE, 0.0f, 100.0f, 0.1f, "%", 1, 0, NULL },
    { "Overlap", NANO_FX_PARAM_RANGE, 0.0f, 100.0f, 0.1f, "%", 1, 0, NULL },
    { "Trig Threshold", NANO_FX_PARAM_RANGE, -65.0f, -10.0f, 0.1f, "dB", 1, 0, NULL },
    { "Dyn Depth", NANO_FX_PARAM_RANGE, 0.0f, 100.0f, 0.1f, "%", 1, 0, NULL },
    { "Dyn Mode", NANO_FX_PARAM_ENUM, 0, 0, 0, "", 0, 3, "Off\nDuck\nGate" },
    { "Threshold", NANO_FX_PARAM_RANGE, -65.0f, -10.0f, 0.1f, "dB", 1, 0, NULL },
    { "Attack", NANO_FX_PARAM_RANGE, 1.0f, 2000.0f, 1.0f, "ms", 0, 0, NULL },
    { "Release", NANO_FX_PARAM_RANGE, 1.0f, 2000.0f, 1.0f, "ms", 0, 0, NULL },
    { "Knee", NANO_FX_PARAM_RANGE, 1.0f, 20.0f, 0.01f, "dB", 2, 0, NULL },
    { "Feedback Depth", NANO_FX_PARAM_RANGE, 0.0f, 100.0f, 0.1f, "%", 1, 0, NULL },
    { "High Pass", NANO_FX_PARAM_RANGE, 20.0f, 800.0f, 1.0f, "Hz", 0, 0, NULL },
    { "Low Pass", NANO_FX_PARAM_RANGE, 1000.0f, 16000.0f, 1.0f, "Hz", 0, 0, NULL },
    { "Link FBack", NANO_FX_PARAM_ENUM, 0, 0, 0, "", 0, 2, "Off\nOn" },
    { "Sync", NANO_FX_PARAM_ENUM, 0, 0, 0, "", 0, 2, "Off\nOn" },
    { "Sync Note L", NANO_FX_PARAM_ENUM, 0, 0, 0, "", 0, 21, "1/64T\n1/64\n1/32T\n1/64D\n1/32\n1/16T\n1/32D\n1/16\n1/8T\n1/16D\n1/8\n1/4T\n1/8D\n1/4\n1/2T\n1/4D\n1/2\n1/1T\n1/2D\n1/1\n1/1D" },
    { "Sync Note R", NANO_FX_PARAM_ENUM, 0, 0, 0, "", 0, 21, "1/64T\n1/64\n1/32T\n1/64D\n1/32\n1/16T\n1/32D\n1/16\n1/8T\n1/16D\n1/8\n1/4T\n1/8D\n1/4\n1/2T\n1/4D\n1/2\n1/1T\n1/2D\n1/1\n1/1D" },
};
static const uint8_t m6014_order[] = { 0, 19, 20, 1, 2, 3, 4, 21, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18 };
static const nano_fx_param_t m6004_params[] = {
    { "Mix", NANO_FX_PARAM_RANGE, 0.0f, 100.0f, 0.1f, "%", 1, 0, NULL },
    { "Feedback", NANO_FX_PARAM_RANGE, 0.0f, 100.0f, 0.1f, "%", 1, 0, NULL },
    { "High Pass", NANO_FX_PARAM_RANGE, 80.0f, 800.0f, 1.0f, "Hz", 0, 0, NULL },
    { "Low Pass", NANO_FX_PARAM_RANGE, 800.0f, 6000.0f, 1.0f, "Hz", 0, 0, NULL },
    { "Drive", NANO_FX_PARAM_RANGE, 0.0f, 100.0f, 1.0f, "%", 0, 0, NULL },
    { "Delay Time", NANO_FX_PARAM_RANGE, 7.0f, 6000.0f, 1.0f, "ms", 0, 0, NULL },
    { "Wow", NANO_FX_PARAM_RANGE, 0.0f, 10.0f, 0.1f, "", 1, 0, NULL },
    { "Flutter", NANO_FX_PARAM_RANGE, 0.0f, 10.0f, 0.1f, "", 1, 0, NULL },
    { "Ping Pong", NANO_FX_PARAM_ENUM, 0, 0, 0, "", 0, 2, "Off\nOn" },
    { "Sync", NANO_FX_PARAM_ENUM, 0, 0, 0, "", 0, 2, "Off\nOn" },
    { "Sync Note", NANO_FX_PARAM_ENUM, 0, 0, 0, "", 0, 21, "1/64T\n1/64\n1/32T\n1/64D\n1/32\n1/16T\n1/32D\n1/16\n1/8T\n1/16D\n1/8\n1/4T\n1/8D\n1/4\n1/2T\n1/4D\n1/2\n1/1T\n1/2D\n1/1\n1/1D" },
};
static const uint8_t m6004_order[] = { 0, 1, 2, 3, 4, 9, 10, 5, 6, 7, 8 };
static const nano_fx_param_t m8008_params[] = {
    { "Mix", NANO_FX_PARAM_RANGE, 0.0f, 100.0f, 0.1f, "%", 1, 0, NULL },
    { "Size", NANO_FX_PARAM_ENUM, 0, 0, 0, "", 0, 3, "Small\nMedium\nLarge" },
    { "Pre Delay", NANO_FX_PARAM_RANGE, 0.0f, 100.0f, 0.1f, "ms", 1, 0, NULL },
    { "High Pass", NANO_FX_PARAM_RANGE, 20.0f, 800.0f, 1.0f, "Hz", 0, 0, NULL },
    { "Low Pass", NANO_FX_PARAM_RANGE, 800.0f, 12000.0f, 1.0f, "Hz", 0, 0, NULL },
};
static const nano_fx_param_t m8009_params[] = {
    { "Mix", NANO_FX_PARAM_RANGE, 0.0f, 100.0f, 0.1f, "%", 1, 0, NULL },
    { "Decay", NANO_FX_PARAM_RANGE, 5.0f, 20.0f, 0.01f, "s", 2, 0, NULL },
    { "Pre Delay", NANO_FX_PARAM_RANGE, 1.0f, 200.0f, 0.1f, "ms", 1, 0, NULL },
    { "Damping", NANO_FX_PARAM_RANGE, 0.0f, 100.0f, 0.1f, "%", 1, 0, NULL },
    { "High Pass", NANO_FX_PARAM_RANGE, 20.0f, 800.0f, 1.0f, "Hz", 0, 0, NULL },
    { "Low Pass", NANO_FX_PARAM_RANGE, 800.0f, 8000.0f, 1.0f, "Hz", 0, 0, NULL },
};
static const nano_fx_param_t m8003_params[] = {
    { "Mix", NANO_FX_PARAM_RANGE, 0.0f, 100.0f, 0.1f, "%", 1, 0, NULL },
    { "Decay", NANO_FX_PARAM_RANGE, 1.0f, 10.0f, 0.01f, "s", 2, 0, NULL },
    { "Pre Delay", NANO_FX_PARAM_RANGE, 1.0f, 100.0f, 0.1f, "ms", 1, 0, NULL },
    { "High Pass", NANO_FX_PARAM_RANGE, 20.0f, 800.0f, 1.0f, "Hz", 0, 0, NULL },
    { "Low Pass", NANO_FX_PARAM_RANGE, 800.0f, 12000.0f, 1.0f, "Hz", 0, 0, NULL },
};
static const nano_fx_param_t m8011_params[] = {
    { "Mix", NANO_FX_PARAM_RANGE, 0.0f, 100.0f, 0.1f, "%", 1, 0, NULL },
    { "Decay", NANO_FX_PARAM_RANGE, 1.0f, 10.0f, 0.01f, "s", 2, 0, NULL },
    { "Pre Delay", NANO_FX_PARAM_RANGE, 1.0f, 100.0f, 0.1f, "ms", 1, 0, NULL },
    { "High Pass", NANO_FX_PARAM_RANGE, 20.0f, 800.0f, 1.0f, "Hz", 0, 0, NULL },
    { "Low Pass", NANO_FX_PARAM_RANGE, 800.0f, 12000.0f, 1.0f, "Hz", 0, 0, NULL },
    { "Damping", NANO_FX_PARAM_RANGE, 0.0f, 100.0f, 0.1f, "%", 1, 0, NULL },
};
static const nano_fx_param_t m8007_params[] = {
    { "Mix", NANO_FX_PARAM_RANGE, 0.0f, 100.0f, 0.1f, "%", 1, 0, NULL },
    { "Decay", NANO_FX_PARAM_RANGE, 1.0f, 10.0f, 0.01f, "s", 2, 0, NULL },
    { "Pre Delay", NANO_FX_PARAM_RANGE, 1.0f, 200.0f, 0.1f, "ms", 1, 0, NULL },
    { "Mod Speed", NANO_FX_PARAM_RANGE, 0.1f, 5.0f, 0.01f, "Hz", 2, 0, NULL },
    { "Mod Depth", NANO_FX_PARAM_RANGE, 0.0f, 100.0f, 0.1f, "%", 1, 0, NULL },
    { "High Pass", NANO_FX_PARAM_RANGE, 20.0f, 800.0f, 1.0f, "Hz", 0, 0, NULL },
    { "Low Pass", NANO_FX_PARAM_RANGE, 800.0f, 12000.0f, 1.0f, "Hz", 0, 0, NULL },
};
static const nano_fx_param_t m8000_params[] = {
    { "Mix", NANO_FX_PARAM_RANGE, 0.0f, 100.0f, 0.1f, "%", 1, 0, NULL },
    { "Decay", NANO_FX_PARAM_RANGE, 0.1f, 2.0f, 0.01f, "s", 2, 0, NULL },
    { "Pre Delay", NANO_FX_PARAM_RANGE, 1.0f, 100.0f, 0.1f, "ms", 1, 0, NULL },
    { "High Pass", NANO_FX_PARAM_RANGE, 20.0f, 800.0f, 1.0f, "Hz", 0, 0, NULL },
    { "Low Pass", NANO_FX_PARAM_RANGE, 800.0f, 12000.0f, 1.0f, "Hz", 0, 0, NULL },
};

static const nano_fx_def_t DEFS[] = {
    { 18, -1, 3, m18_params, NULL }, /* Chief BD2 */
    { 13, -1, 2, m13_params, NULL }, /* Chief OD1 */
    { 6, -1, 4, m6_params, NULL }, /* Exotic */
    { 3007, -1, 4, m3007_params, NULL }, /* Exotic Bass Z Boost */
    { 23, -1, 4, m23_params, NULL }, /* Exotic Z Boost */
    { 22, -1, 4, m22_params, NULL }, /* Facial Fuzz */
    { 27, -1, 3, m27_params, NULL }, /* Green 808 */
    { 3000, -1, 6, m3000_params, NULL }, /* Microtubes B3K */
    { 3, -1, 2, m3_params, NULL }, /* OD250 */
    { 2, -1, 4, m2_params, NULL }, /* Obsessive Drive */
    { 4, -1, 3, m4_params, NULL }, /* Rodent Drive */
    { 16001, -1, 1, m16001_params, NULL }, /* Adaptive Gate */
    { 4005, -1, 12, m4005_params, NULL }, /* Graphic 9 */
    { 4003, -1, 5, m4003_params, NULL }, /* Low-High Cut */
    { 4001, -1, 16, m4001_params, NULL }, /* Parametric 3 */
    { 16002, -1, 5, m16002_params, NULL }, /* Utility Gate */
    { 16006, -1, 2, m16006_params, NULL }, /* Volume */
    { 9012, -1, 1, m9012_params, NULL }, /* Bass Wah */
    { 9010, -1, 1, m9010_params, NULL }, /* Bubba Wah */
    { 9014, -1, 1, m9014_params, NULL }, /* Crying Clyde Wah */
    { 9013, -1, 1, m9013_params, NULL }, /* Crying Wah */
    { 24006, 8, 9, m24006_params, NULL }, /* Envelope Filter */
    { 24001, -1, 11, m24001_params, NULL }, /* Love Meat */
    { 5001, 5, 6, m5001_params, NULL }, /* Legendary 87 (M) */
    { 5007, 5, 6, m5007_params, NULL }, /* Opto Comp (M) */
    { 5004, 5, 6, m5004_params, NULL }, /* Solid State Comp (M) */
    { 5005, 5, 6, m5005_params, NULL }, /* VCA Comp (M) */
    { 18001, 0, 5, m18001_params, NULL }, /* Transpose */
    { 16011, -1, 3, m16011_params, NULL }, /* Doubler */
    { 5012, 5, 6, m5012_params, NULL }, /* Legendary 87 (ST) */
    { 5015, 5, 6, m5015_params, NULL }, /* Opto Comp (ST) */
    { 5013, 5, 6, m5013_params, NULL }, /* Solid State Comp (ST) */
    { 5014, 5, 6, m5014_params, NULL }, /* VCA Comp (ST) */
    { 7024, 0, 8, m7024_params, m7024_order }, /* Chief CE2W (ST) */
    { 7027, 0, 5, m7027_params, NULL }, /* Chief DC2W (ST) */
    { 7023, 0, 7, m7023_params, m7023_order }, /* Chorus 229T */
    { 7022, 0, 7, m7022_params, m7022_order }, /* Dream Chorus */
    { 7021, 0, 8, m7021_params, m7021_order }, /* MX Flanger */
    { 7028, 0, 7, m7028_params, m7028_order }, /* MX Phase 95 */
    { 7029, 0, 8, m7029_params, m7029_order }, /* MX Vibe */
    { 7004, -1, 12, m7004_params, m7004_order }, /* Tremolo */
    { 6010, 0, 12, m6010_params, m6010_order }, /* Analog Delay */
    { 6015, 0, 12, m6015_params, m6015_order }, /* Circular Delay */
    { 6011, 0, 18, m6011_params, m6011_order }, /* Digital Delay (ST) */
    { 6012, 0, 22, m6012_params, m6012_order }, /* Dual Delay */
    { 6014, 0, 22, m6014_params, m6014_order }, /* Dual Reverse Delay */
    { 6004, 0, 11, m6004_params, m6004_order }, /* Tape Delay */
    { 8008, 0, 5, m8008_params, NULL }, /* Ambience */
    { 8009, 0, 6, m8009_params, NULL }, /* Cave */
    { 8003, 0, 5, m8003_params, NULL }, /* Hall */
    { 8011, 0, 6, m8011_params, NULL }, /* Mind Hall */
    { 8007, 0, 7, m8007_params, NULL }, /* Modulated */
    { 8000, 0, 5, m8000_params, NULL }, /* Room */
};

static const uint16_t SLOT0[] = { 18, 13, 6, 3007, 23, 22, 27, 3000, 3, 2, 4, 16001, 4005, 4003, 4001, 16002, 16006, 9012, 9010, 9014, 9013, 24006, 24001, 5001, 5007, 5004, 5005, 18001 };
static const uint16_t SLOT1[] = { 18, 13, 6, 3007, 23, 22, 27, 3000, 3, 2, 4, 16001, 4005, 4003, 4001, 16002, 16006, 9012, 9010, 9014, 9013, 24006, 24001, 5001, 5007, 5004, 5005, 18001 };
static const uint16_t SLOT2[] = { 16011, 4005, 4003, 4001, 5012, 5015, 5013, 5014, 7024, 7027, 7023, 7022, 7021, 7028, 7029, 7004 };
static const uint16_t SLOT3[] = { 4005, 4003, 4001, 5012, 5015, 5013, 5014, 6010, 6015, 6011, 6012, 6014, 6004 };
static const uint16_t SLOT4[] = { 4005, 4003, 4001, 5012, 5015, 5013, 5014, 8008, 8009, 8003, 8011, 8007, 8000 };
static const uint16_t *const SLOTS[NANO_FX_SLOT_COUNT] = { SLOT0, SLOT1, SLOT2, SLOT3, SLOT4 };
static const uint8_t SLOT_COUNTS[NANO_FX_SLOT_COUNT] = { 28, 28, 16, 13, 13 };

const nano_fx_def_t *nano_fx_def(uint32_t type)
{
    for (size_t i = 0; type && i < sizeof(DEFS) / sizeof(DEFS[0]); i++) {
        if (DEFS[i].type == type) return &DEFS[i];
    }
    return NULL;
}

const uint16_t *nano_fx_slot_models(int slot, int *count)
{
    if (slot < 0 || slot >= NANO_FX_SLOT_COUNT) {
        *count = 0;
        return NULL;
    }
    *count = SLOT_COUNTS[slot];
    return SLOTS[slot];
}
