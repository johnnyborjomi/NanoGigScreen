#include "nano_scales.h"

#include <math.h>
#include <stdbool.h>

#define CAP_VOL_MIN_DB (-24.0f)
#define CAP_VOL_SPAN_DB 36.0f
#define CAP_VOL_CURVE 1.708f

float nano_capture_volume_db(uint8_t raw)
{
    return CAP_VOL_SPAN_DB * powf(raw / 255.0f, 1.0f / CAP_VOL_CURVE) + CAP_VOL_MIN_DB;
}

uint8_t nano_capture_volume_raw(float db)
{
    float x = (db - CAP_VOL_MIN_DB) / CAP_VOL_SPAN_DB;
    if (x <= 0) return 0;
    if (x >= 1) return 255;
    return (uint8_t)lroundf(255.0f * powf(x, CAP_VOL_CURVE));
}

int nano_capture_volume_tenths(uint8_t raw)
{
    float tenths = nano_capture_volume_db(raw) * 10.0f;
    return (int)truncf(tenths < 0 ? tenths - 0.1f : tenths + 0.1f);
}

#define CAB_LEVEL_CURVE 3.5f
#define CAB_FILTER_CURVE (5.0f / 3.0f)

/* Each parameter: value = min + span * n^exponent. */
static bool cab_scale(nano_cab_param_t param, float *min, float *span, float *exponent)
{
    switch (param) {
    case NANO_CAB_LEVEL:
        *min = NANO_CAB_LEVEL_MIN_DB, *span = NANO_CAB_LEVEL_MAX_DB - NANO_CAB_LEVEL_MIN_DB, *exponent = 1 / CAB_LEVEL_CURVE;
        return true;
    case NANO_CAB_HIGH_PASS:
        *min = NANO_CAB_HIGH_PASS_MIN_HZ, *span = NANO_CAB_HIGH_PASS_MAX_HZ - NANO_CAB_HIGH_PASS_MIN_HZ, *exponent = CAB_FILTER_CURVE;
        return true;
    case NANO_CAB_LOW_PASS:
        *min = NANO_CAB_LOW_PASS_MIN_HZ, *span = NANO_CAB_LOW_PASS_MAX_HZ - NANO_CAB_LOW_PASS_MIN_HZ, *exponent = CAB_FILTER_CURVE;
        return true;
    default:
        return false;
    }
}

float nano_cab_value(nano_cab_param_t param, float n)
{
    float min, span, e;
    if (!cab_scale(param, &min, &span, &e)) return 0;
    n = n < 0 ? 0 : n > 1 ? 1 : n;
    return min + powf(n, e) * span;
}

float nano_cab_normalized(nano_cab_param_t param, float v)
{
    float min, span, e;
    if (!cab_scale(param, &min, &span, &e)) return 0;
    float x = (v - min) / span;
    float n = x > 0 ? powf(x, 1 / e) : 0;
    return n > 1 ? 1 : n;
}
