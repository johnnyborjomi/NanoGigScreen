#include "nano_proto.h"

#include <string.h>

size_t nano_read_varint(const uint8_t *data, size_t len, uint64_t *out)
{
    uint64_t value = 0;
    size_t n = len < 10 ? len : 10;
    for (size_t i = 0; i < n; i++) {
        uint8_t b = data[i];
        value |= (uint64_t)(b & 0x7f) << (7 * i);
        if ((b & 0x80) == 0) {
            if (out) *out = value;
            return i + 1;
        }
    }
    return 0;
}

size_t nano_write_varint(uint8_t *out, size_t cap, uint64_t value)
{
    size_t n = 0;
    do {
        if (n >= cap) return 0;
        uint8_t b = value & 0x7f;
        value >>= 7;
        out[n++] = value ? (b | 0x80) : b;
    } while (value);
    return n;
}

void nano_proto_iter_init(nano_proto_iter_t *it, const uint8_t *data, size_t len)
{
    it->data = data;
    it->len = len;
    it->pos = 0;
}

bool nano_proto_next(nano_proto_iter_t *it, nano_field_t *f)
{
    if (it->pos >= it->len) return false;
    uint64_t tag;
    size_t n = nano_read_varint(it->data + it->pos, it->len - it->pos, &tag);
    if (!n) goto stop;
    uint32_t field = (uint32_t)(tag >> 3);
    uint32_t wire = (uint32_t)(tag & 7);
    size_t i = it->pos + n;
    if (field == 0) goto stop;
    f->field = field;
    f->wire = (nano_wire_t)wire;
    f->value = 0;
    switch (wire) {
    case NANO_WIRE_VARINT: {
        uint64_t v;
        size_t vn = nano_read_varint(it->data + i, it->len - i, &v);
        if (!vn) goto stop;
        f->raw = it->data + i;
        f->len = vn;
        f->value = v;
        it->pos = i + vn;
        return true;
    }
    case NANO_WIRE_FIXED64:
        if (i + 8 > it->len) goto stop;
        f->raw = it->data + i;
        f->len = 8;
        it->pos = i + 8;
        return true;
    case NANO_WIRE_BYTES: {
        uint64_t l;
        size_t ln = nano_read_varint(it->data + i, it->len - i, &l);
        if (!ln) goto stop;
        size_t start = i + ln;
        if (l > it->len - start) goto stop;
        f->raw = it->data + start;
        f->len = (size_t)l;
        it->pos = start + (size_t)l;
        return true;
    }
    case NANO_WIRE_FIXED32:
        if (i + 4 > it->len) goto stop;
        f->raw = it->data + i;
        f->len = 4;
        it->pos = i + 4;
        return true;
    default:
        goto stop; /* groups / unknown wire types: fail soft */
    }
stop:
    it->pos = it->len;
    return false;
}

bool nano_first_field(const uint8_t *data, size_t len, uint32_t field, nano_field_t *out)
{
    nano_proto_iter_t it;
    nano_field_t f;
    nano_proto_iter_init(&it, data, len);
    while (nano_proto_next(&it, &f)) {
        if (f.field == field) {
            if (out) *out = f;
            return true;
        }
    }
    return false;
}

bool nano_has_field(const uint8_t *data, size_t len, uint32_t field)
{
    return nano_first_field(data, len, field, NULL);
}

int64_t nano_first_varint(const uint8_t *data, size_t len, uint32_t field, int64_t dflt)
{
    nano_proto_iter_t it;
    nano_field_t f;
    nano_proto_iter_init(&it, data, len);
    while (nano_proto_next(&it, &f)) {
        if (f.field == field && f.wire == NANO_WIRE_VARINT) return (int64_t)f.value;
    }
    return dflt;
}

bool nano_first_bytes(const uint8_t *data, size_t len, uint32_t field, const uint8_t **out, size_t *out_len)
{
    nano_proto_iter_t it;
    nano_field_t f;
    nano_proto_iter_init(&it, data, len);
    while (nano_proto_next(&it, &f)) {
        if (f.field == field && f.wire == NANO_WIRE_BYTES) {
            *out = f.raw;
            *out_len = f.len;
            return true;
        }
    }
    return false;
}

bool nano_first_fixed32_float(const uint8_t *data, size_t len, uint32_t field, float *out)
{
    nano_proto_iter_t it;
    nano_field_t f;
    nano_proto_iter_init(&it, data, len);
    while (nano_proto_next(&it, &f)) {
        if (f.field == field && f.wire == NANO_WIRE_FIXED32) {
            uint32_t u = (uint32_t)f.raw[0] | ((uint32_t)f.raw[1] << 8) | ((uint32_t)f.raw[2] << 16) | ((uint32_t)f.raw[3] << 24);
            memcpy(out, &u, 4);
            return true;
        }
    }
    return false;
}

/* Text = no control characters. Bytes >= 0x80 are allowed so UTF-8 names (Cortex Cloud lets
 * users type them) survive; the display falls back to a blank glyph for what its font lacks. */
static bool printable(const uint8_t *p, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        if (p[i] < 0x20 || p[i] == 0x7f) return false;
    }
    return true;
}

size_t nano_first_string(const uint8_t *data, size_t len, uint32_t field, char *out, size_t cap)
{
    if (cap == 0) return 0;
    out[0] = '\0';
    nano_proto_iter_t it;
    nano_field_t f;
    nano_proto_iter_init(&it, data, len);
    while (nano_proto_next(&it, &f)) {
        if (f.field != field || f.wire != NANO_WIRE_BYTES) continue;
        if (!printable(f.raw, f.len)) continue;
        size_t n = f.len < cap - 1 ? f.len : cap - 1;
        memcpy(out, f.raw, n);
        out[n] = '\0';
        return n;
    }
    return 0;
}
