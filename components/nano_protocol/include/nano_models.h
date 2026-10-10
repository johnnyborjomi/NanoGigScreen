/*
 * FX model catalogue: state fields 48-52 model ID (raw bytes as uppercase hex)
 * -> name and category. Port of NanoGig `src/protocol/models.ts` (from
 * choldy/nano-cortex-web-editor, MIT). Colours per category are the NanoGig
 * palette (`src/ui/styles.css`, sampled from Cortex Cloud 2026-09-12).
 */
#ifndef NANO_MODELS_H
#define NANO_MODELS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    NANO_CAT_UNKNOWN = 0,
    NANO_CAT_OVERDRIVE,
    NANO_CAT_COMPRESSOR,
    NANO_CAT_MODULATION,
    NANO_CAT_DELAY,
    NANO_CAT_REVERB,
    NANO_CAT_PITCH,
    NANO_CAT_WAH,
    NANO_CAT_FILTER,
    NANO_CAT_UTILITY,
    NANO_CAT_EQ,
    NANO_CAT_COUNT,
} nano_category_t;

typedef struct {
    const char *id;   /* uppercase hex of the field's raw bytes, e.g. "FA2E" */
    const char *name;
    nano_category_t category;
} nano_fx_model_t;

/* NULL when the ID is not in the catalogue. `id_hex` must be uppercase, no spaces. */
const nano_fx_model_t *nano_lookup_fx_model(const char *id_hex);
/* The model's type: the varint its ID spells ("FA2E" = 6010), what nano_fx_params.h and the model write use. */
uint32_t nano_fx_model_type(const nano_fx_model_t *m);
/* NULL when no catalogue model has this type. */
const nano_fx_model_t *nano_fx_model_by_type(uint32_t type);
const char *nano_category_name(nano_category_t c);
/* Short tag for the tiles ("CMP", "DRV", "PTCH", ...); "" for unknown. */
const char *nano_category_short(nano_category_t c);
/* 0xRRGGBB tile colour for the category. */
uint32_t nano_category_color(nano_category_t c);
/* True when dark text is unreadable on the category colour (indigo modulation, royal blue EQ). */
bool nano_category_light_text(nano_category_t c);

#ifdef __cplusplus
}
#endif
#endif
