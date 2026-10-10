/*
 * FX parameters per model, as the Nano Cortex Editor defines them (from DrD85/nano-cortex-controller, MIT; generated
 * by tools/gen_fx_params.py into nano_fx_params.c), and the models each FX slot takes. A model is named by its type:
 * the varint value of state fields 48-52 (nano_fx_model_type), e.g. 6010 = Analog Delay.
 *
 * The pedal keeps every parameter as 0..1 (nano_build_fx_param, nano_decode_fx_params). A range parameter shows as
 * min + n * (max - min) in `step`s; an enum as option round(n * (option_count - 1)).
 */
#ifndef NANO_FX_PARAMS_H
#define NANO_FX_PARAMS_H

#include <stdint.h>

#include "nano_frame.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NANO_FX_PARAMS_MAX 24   /* the most any model has is 22 (the dual delays) */

typedef enum { NANO_FX_PARAM_RANGE, NANO_FX_PARAM_ENUM } nano_fx_param_kind_t;

typedef struct {
    const char *name;
    uint8_t kind;               /* nano_fx_param_kind_t */
    float min, max, step;       /* range parameters */
    const char *unit;           /* "", "%", "dB", "Hz", "ms", "s", "Cent", "Sem" */
    int8_t decimals;
    uint8_t option_count;       /* enum parameters */
    const char *options;        /* enum options separated by "\n" */
} nano_fx_param_t;

typedef struct {
    uint32_t type;
    int8_t mix_param;           /* index of "Mix", -1 = none */
    uint8_t param_count;
    const nano_fx_param_t *params;
    const uint8_t *order;       /* the editor's display order (indexes), NULL = as defined */
} nano_fx_def_t;

/* NULL for an unknown type (and 0 = an empty slot). */
const nano_fx_def_t *nano_fx_def(uint32_t type);
/* The models FX slot `slot` (0..4 = pre1..post3) takes, in the editor's order; NULL / 0 past the slots. */
const uint16_t *nano_fx_slot_models(int slot, int *count);

#ifdef __cplusplus
}
#endif
#endif
