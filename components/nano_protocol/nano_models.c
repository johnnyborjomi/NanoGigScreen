#include "nano_models.h"

#include <string.h>

/* Categories follow Neural's device list (neuraldsp.com/nano-cortex-device-list, 2026-10-01) and
 * Cortex Cloud's colouring: the "(ST)" compressors are compressors (upstream filed them under
 * Utility/EQ, which painted a Post-slot compressor white), Doubler is a utility, EQ is its own
 * category, and Wah and Filter are separate. Mirrors NanoGig `src/protocol/models.ts`. */
static const nano_fx_model_t MODELS[] = {
    { "12", "Chief BD2", NANO_CAT_OVERDRIVE },
    { "0D", "Chief OD1", NANO_CAT_OVERDRIVE },
    { "06", "Exotic", NANO_CAT_OVERDRIVE },
    { "BF17", "Exotic Bass Z Boost", NANO_CAT_OVERDRIVE },
    { "17", "Exotic Z Boost", NANO_CAT_OVERDRIVE },
    { "16", "Facial Fuzz", NANO_CAT_OVERDRIVE },
    { "1B", "Green 808", NANO_CAT_OVERDRIVE },
    { "B817", "Microtubes B3K", NANO_CAT_OVERDRIVE },
    { "03", "OD250", NANO_CAT_OVERDRIVE },
    { "02", "Obsessive Drive", NANO_CAT_OVERDRIVE },
    { "04", "Rodent Drive", NANO_CAT_OVERDRIVE },
    { "817D", "Adaptive Gate", NANO_CAT_UTILITY },
    { "827D", "Utility Gate", NANO_CAT_UTILITY },
    { "867D", "Volume", NANO_CAT_UTILITY },
    { "B446", "Bass Wah", NANO_CAT_WAH },
    { "B246", "Bubba Wah", NANO_CAT_WAH },
    { "B646", "Crying Clyde Wah", NANO_CAT_WAH },
    { "B546", "Crying Wah", NANO_CAT_WAH },
    { "C6BB01", "Envelope Filter", NANO_CAT_FILTER },
    { "C1BB01", "Love Meat", NANO_CAT_FILTER },
    { "8927", "Legendary 87 (M)", NANO_CAT_COMPRESSOR },
    { "8F27", "Opto Comp (M)", NANO_CAT_COMPRESSOR },
    { "8C27", "Solid State Comp (M)", NANO_CAT_COMPRESSOR },
    { "8D27", "VCA Comp (M)", NANO_CAT_COMPRESSOR },
    { "D18C01", "Transpose", NANO_CAT_PITCH },
    { "8B7D", "Doubler", NANO_CAT_UTILITY },
    { "A51F", "Graphic 9", NANO_CAT_EQ },
    { "A31F", "Low-High Cut", NANO_CAT_EQ },
    { "A11F", "Parametric 3", NANO_CAT_EQ },
    { "9427", "Legendary 87 (ST)", NANO_CAT_COMPRESSOR },
    { "9727", "Opto Comp (ST)", NANO_CAT_COMPRESSOR },
    { "9527", "Solid State Comp (ST)", NANO_CAT_COMPRESSOR },
    { "9627", "VCA Comp (ST)", NANO_CAT_COMPRESSOR },
    { "F036", "Chief CE2W (ST)", NANO_CAT_MODULATION },
    { "F336", "Chief DC2W (ST)", NANO_CAT_MODULATION },
    { "EF36", "Chorus 229T", NANO_CAT_MODULATION },
    { "EE36", "Dream Chorus", NANO_CAT_MODULATION },
    { "ED36", "MX Flanger", NANO_CAT_MODULATION },
    { "F436", "MX Phase 95", NANO_CAT_MODULATION },
    { "F536", "MX Vibe", NANO_CAT_MODULATION },
    { "DC36", "Tremolo", NANO_CAT_MODULATION },
    { "FA2E", "Analog Delay", NANO_CAT_DELAY },
    { "FF2E", "Circular Delay", NANO_CAT_DELAY },
    { "FB2E", "Digital Delay (ST)", NANO_CAT_DELAY },
    { "FC2E", "Dual Delay", NANO_CAT_DELAY },
    { "FE2E", "Dual Reverse Delay", NANO_CAT_DELAY },
    { "F42E", "Tape Delay", NANO_CAT_DELAY },
    { "C83E", "Ambience", NANO_CAT_REVERB },
    { "C93E", "Cave", NANO_CAT_REVERB },
    { "C33E", "Hall", NANO_CAT_REVERB },
    { "CB3E", "Mind Hall", NANO_CAT_REVERB },
    { "C73E", "Modulated", NANO_CAT_REVERB },
    { "C03E", "Room", NANO_CAT_REVERB },
};

const nano_fx_model_t *nano_lookup_fx_model(const char *id_hex)
{
    if (!id_hex || !*id_hex) return NULL;
    for (size_t i = 0; i < sizeof(MODELS) / sizeof(MODELS[0]); i++) {
        if (strcmp(MODELS[i].id, id_hex) == 0) return &MODELS[i];
    }
    return NULL;
}

static const char *const CATEGORY_NAMES[NANO_CAT_COUNT] = {
    "Unknown", "Overdrive", "Compressor", "Modulation", "Delay", "Reverb", "Pitch", "Wah", "Filter", "Utility", "EQ",
};

const char *nano_category_name(nano_category_t c)
{
    return (c >= 0 && c < NANO_CAT_COUNT) ? CATEGORY_NAMES[c] : CATEGORY_NAMES[0];
}

static const char *const CATEGORY_SHORT[NANO_CAT_COUNT] = {
    "", "DRV", "CMP", "MOD", "DLY", "RVB", "PTCH", "WAH", "FLT", "UTL", "EQ",
};

const char *nano_category_short(nano_category_t c)
{
    return (c >= 0 && c < NANO_CAT_COUNT) ? CATEGORY_SHORT[c] : CATEGORY_SHORT[0];
}

/* NanoGig `--fx-*` tokens. Reverb is shifted to azure so it stays apart from delay on stage. */
static const uint32_t CATEGORY_COLORS[NANO_CAT_COUNT] = {
    0xA8A29E, /* unknown */
    0xFF6A00, /* overdrive */
    0x4CF06A, /* compressor */
    0x3A0CF5, /* modulation (indigo) */
    0x00F0D8, /* delay */
    0x3D9BFF, /* reverb */
    0xFFD23F, /* pitch */
    0xC2E3F4, /* wah: white in the app, shifted halfway to the filter blue so it is not a utility */
    0x93D4F5, /* filter (sky blue, sampled 2026-10-01) */
    0xF2F2F2, /* utility */
    0x3A62D6, /* eq (royal blue, sampled 2026-10-01) */
};

uint32_t nano_category_color(nano_category_t c)
{
    return (c >= 0 && c < NANO_CAT_COUNT) ? CATEGORY_COLORS[c] : CATEGORY_COLORS[0];
}

bool nano_category_light_text(nano_category_t c)
{
    return c == NANO_CAT_MODULATION || c == NANO_CAT_EQ;
}
