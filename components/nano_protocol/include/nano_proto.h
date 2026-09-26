/*
 * Minimal protobuf wire-format walker for the Nano Cortex messages.
 * Port of NanoGig `src/protocol/proto.ts`. No allocation, never faults on
 * malformed input: the iterator simply stops and reports what it parsed.
 */
#ifndef NANO_PROTO_H
#define NANO_PROTO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    NANO_WIRE_VARINT = 0,
    NANO_WIRE_FIXED64 = 1,
    NANO_WIRE_BYTES = 2,
    NANO_WIRE_FIXED32 = 5,
} nano_wire_t;

typedef struct {
    uint32_t field;
    nano_wire_t wire;
    const uint8_t *raw; /* varint bytes, 4/8 fixed bytes, or the delimited payload */
    size_t len;
    uint64_t value;     /* decoded varint for NANO_WIRE_VARINT */
} nano_field_t;

typedef struct {
    const uint8_t *data;
    size_t len;
    size_t pos;
} nano_proto_iter_t;

/* Read a base-128 varint. Returns bytes consumed (1..10) or 0 when truncated. */
size_t nano_read_varint(const uint8_t *data, size_t len, uint64_t *out);

void nano_proto_iter_init(nano_proto_iter_t *it, const uint8_t *data, size_t len);
/* Next top-level field; false at the end or on malformed input (fail soft). */
bool nano_proto_next(nano_proto_iter_t *it, nano_field_t *f);

/* First-match helpers over a whole message. */
bool nano_first_field(const uint8_t *data, size_t len, uint32_t field, nano_field_t *out);
/* Varint value of `field`, or `dflt` when absent (proto3 default semantics: absent = 0). */
int64_t nano_first_varint(const uint8_t *data, size_t len, uint32_t field, int64_t dflt);
bool nano_has_field(const uint8_t *data, size_t len, uint32_t field);
bool nano_first_bytes(const uint8_t *data, size_t len, uint32_t field, const uint8_t **out, size_t *out_len);
bool nano_first_fixed32_float(const uint8_t *data, size_t len, uint32_t field, float *out);
/*
 * Copy the first text field (no control characters; UTF-8 allowed) into `out`
 * (NUL-terminated, truncated to cap-1). Binary payloads are skipped.
 * Returns the copied length; `out` is "" when nothing matched.
 */
size_t nano_first_string(const uint8_t *data, size_t len, uint32_t field, char *out, size_t cap);

/* Encoding (used by request builders and tests). Returns bytes written. */
size_t nano_write_varint(uint8_t *out, size_t cap, uint64_t value);

#ifdef __cplusplus
}
#endif
#endif
