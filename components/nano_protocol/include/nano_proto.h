/*
 * Minimal protobuf wire format for the Nano Cortex messages: a reader (port of NanoGig `src/protocol/proto.ts`)
 * and a writer for the request frames. No allocation. The reader never faults on malformed input: the iterator
 * stops and reports what it parsed; the writer stops writing when the buffer is full and reports failure.
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

/* ---- reading ------------------------------------------------------------ */

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

/* A fixed32 field as a float (little-endian, like the pedal). */
float nano_field_f32(const nano_field_t *f);
/*
 * A bytes field as text (no control characters; UTF-8 allowed) into `out`, NUL-terminated and truncated to cap-1.
 * Returns the copied length; 0 and "" when the field is not text.
 */
size_t nano_field_text(const nano_field_t *f, char *out, size_t cap);

/*
 * First-match reads over a whole message: the first `field` of the right wire type. Absent fields are the norm,
 * not an error: proto3 leaves zeros out, so the caller decides what absent means.
 */
bool nano_get_field(const uint8_t *data, size_t len, uint32_t field, nano_field_t *out); /* any wire type */
bool nano_has_field(const uint8_t *data, size_t len, uint32_t field);
bool nano_get_varint(const uint8_t *data, size_t len, uint32_t field, uint64_t *out);
bool nano_get_f32(const uint8_t *data, size_t len, uint32_t field, float *out);
bool nano_get_bytes(const uint8_t *data, size_t len, uint32_t field, const uint8_t **out, size_t *out_len);
/* First text field (binary payloads are skipped); "" and 0 when none. */
size_t nano_get_text(const uint8_t *data, size_t len, uint32_t field, char *out, size_t cap);
/* Varint clamped to `max`; `absent` when the field is missing. */
uint32_t nano_get_uint(const uint8_t *data, size_t len, uint32_t field, uint32_t max, uint32_t absent);
/* Varint `field` as an index below `count`: `absent` when missing, -1 when present but out of range. */
int nano_get_index(const uint8_t *data, size_t len, uint32_t field, int count, int absent);

/* ---- writing ------------------------------------------------------------ */

/*
 * Field-by-field writer. Every value is written, zeros included: the pedal-verified frames carry `28 00`,
 * position 0 and the like, so there is no proto3 zero omission here. Once a write does not fit, `ok` goes false
 * and the writer stops writing.
 */
typedef struct {
    uint8_t *buf;
    size_t cap, pos;
    bool ok;
} nano_pb_writer_t;

void nano_pb_init(nano_pb_writer_t *w, uint8_t *out, size_t cap);
void nano_pb_raw(nano_pb_writer_t *w, const void *data, size_t len); /* bytes as they are (no tag) */
void nano_pb_varint(nano_pb_writer_t *w, uint32_t field, uint64_t value);
void nano_pb_int(nano_pb_writer_t *w, uint32_t field, int64_t value); /* -1 = FF x9 01, as the pedal expects */
void nano_pb_f32(nano_pb_writer_t *w, uint32_t field, float value);
void nano_pb_bytes(nano_pb_writer_t *w, uint32_t field, const void *data, size_t len);
void nano_pb_text(nano_pb_writer_t *w, uint32_t field, const char *text);
/* Sub-message: begin returns a mark for end, which fills in the length (one byte: the payload must stay < 128). */
size_t nano_pb_begin(nano_pb_writer_t *w, uint32_t field);
void nano_pb_end(nano_pb_writer_t *w, size_t mark);

/* Encoding of one varint. Returns bytes written, 0 when it does not fit. */
size_t nano_write_varint(uint8_t *out, size_t cap, uint64_t value);

#ifdef __cplusplus
}
#endif
#endif
