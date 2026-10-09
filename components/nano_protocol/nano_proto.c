#include "nano_proto.h"

#include <string.h>

/* ---- reading ------------------------------------------------------------ */

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
        if (8 > it->len - i) goto stop;
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
        if (4 > it->len - i) goto stop;
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

float nano_field_f32(const nano_field_t *f)
{
    uint32_t u = (uint32_t)f->raw[0] | ((uint32_t)f->raw[1] << 8) | ((uint32_t)f->raw[2] << 16) | ((uint32_t)f->raw[3] << 24);
    float v;
    memcpy(&v, &u, 4);
    return v;
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

size_t nano_field_text(const nano_field_t *f, char *out, size_t cap)
{
    if (cap == 0) return 0;
    out[0] = '\0';
    if (f->wire != NANO_WIRE_BYTES || !printable(f->raw, f->len)) return 0;
    size_t n = f->len < cap - 1 ? f->len : cap - 1;
    memcpy(out, f->raw, n);
    out[n] = '\0';
    return n;
}

/* First `field` of wire type `wire` (any wire type when wire < 0). */
static bool find(const uint8_t *data, size_t len, uint32_t field, int wire, nano_field_t *out)
{
    nano_proto_iter_t it;
    nano_field_t f;
    nano_proto_iter_init(&it, data, len);
    while (nano_proto_next(&it, &f)) {
        if (f.field == field && (wire < 0 || (int)f.wire == wire)) {
            if (out) *out = f;
            return true;
        }
    }
    return false;
}

bool nano_get_field(const uint8_t *data, size_t len, uint32_t field, nano_field_t *out)
{
    return find(data, len, field, -1, out);
}

bool nano_has_field(const uint8_t *data, size_t len, uint32_t field)
{
    return find(data, len, field, -1, NULL);
}

bool nano_get_varint(const uint8_t *data, size_t len, uint32_t field, uint64_t *out)
{
    nano_field_t f;
    if (!find(data, len, field, NANO_WIRE_VARINT, &f)) return false;
    *out = f.value;
    return true;
}

bool nano_get_f32(const uint8_t *data, size_t len, uint32_t field, float *out)
{
    nano_field_t f;
    if (!find(data, len, field, NANO_WIRE_FIXED32, &f)) return false;
    *out = nano_field_f32(&f);
    return true;
}

bool nano_get_bytes(const uint8_t *data, size_t len, uint32_t field, const uint8_t **out, size_t *out_len)
{
    nano_field_t f;
    if (!find(data, len, field, NANO_WIRE_BYTES, &f)) return false;
    *out = f.raw;
    *out_len = f.len;
    return true;
}

size_t nano_get_text(const uint8_t *data, size_t len, uint32_t field, char *out, size_t cap)
{
    if (cap == 0) return 0;
    out[0] = '\0';
    nano_proto_iter_t it;
    nano_field_t f;
    nano_proto_iter_init(&it, data, len);
    while (nano_proto_next(&it, &f)) {
        if (f.field != field) continue;
        size_t n = nano_field_text(&f, out, cap);
        if (n) return n;
    }
    return 0;
}

uint32_t nano_get_uint(const uint8_t *data, size_t len, uint32_t field, uint32_t max, uint32_t absent)
{
    uint64_t v;
    if (!nano_get_varint(data, len, field, &v)) return absent;
    return v > max ? max : (uint32_t)v;
}

int nano_get_index(const uint8_t *data, size_t len, uint32_t field, int count, int absent)
{
    uint64_t v;
    if (!nano_get_varint(data, len, field, &v)) return absent;
    return v < (uint64_t)count ? (int)v : -1;
}

/* ---- writing ------------------------------------------------------------ */

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

void nano_pb_init(nano_pb_writer_t *w, uint8_t *out, size_t cap)
{
    w->buf = out;
    w->cap = cap;
    w->pos = 0;
    w->ok = out != NULL;
}

void nano_pb_raw(nano_pb_writer_t *w, const void *data, size_t len)
{
    if (!w->ok || len > w->cap - w->pos) {
        w->ok = false;
        return;
    }
    memcpy(w->buf + w->pos, data, len);
    w->pos += len;
}

static void put_varint(nano_pb_writer_t *w, uint64_t v)
{
    if (!w->ok) return;
    size_t n = nano_write_varint(w->buf + w->pos, w->cap - w->pos, v);
    if (!n) w->ok = false;
    w->pos += n;
}

static void put_tag(nano_pb_writer_t *w, uint32_t field, nano_wire_t wire)
{
    put_varint(w, (uint64_t)field << 3 | wire);
}

void nano_pb_varint(nano_pb_writer_t *w, uint32_t field, uint64_t value)
{
    put_tag(w, field, NANO_WIRE_VARINT);
    put_varint(w, value);
}

void nano_pb_int(nano_pb_writer_t *w, uint32_t field, int64_t value)
{
    nano_pb_varint(w, field, (uint64_t)value);
}

void nano_pb_f32(nano_pb_writer_t *w, uint32_t field, float value)
{
    uint32_t u;
    memcpy(&u, &value, 4);
    const uint8_t le[4] = { (uint8_t)u, (uint8_t)(u >> 8), (uint8_t)(u >> 16), (uint8_t)(u >> 24) };
    put_tag(w, field, NANO_WIRE_FIXED32);
    nano_pb_raw(w, le, 4);
}

void nano_pb_bytes(nano_pb_writer_t *w, uint32_t field, const void *data, size_t len)
{
    put_tag(w, field, NANO_WIRE_BYTES);
    put_varint(w, len);
    nano_pb_raw(w, data, len);
}

void nano_pb_text(nano_pb_writer_t *w, uint32_t field, const char *text)
{
    nano_pb_bytes(w, field, text, text ? strlen(text) : 0);
}

size_t nano_pb_begin(nano_pb_writer_t *w, uint32_t field)
{
    put_tag(w, field, NANO_WIRE_BYTES);
    size_t mark = w->pos;
    const uint8_t placeholder = 0;
    nano_pb_raw(w, &placeholder, 1);
    return mark;
}

void nano_pb_end(nano_pb_writer_t *w, size_t mark)
{
    if (!w->ok) return;
    size_t n = w->pos - mark - 1;
    if (n > 0x7f) {
        w->ok = false;
        return;
    }
    w->buf[mark] = (uint8_t)n;
}
