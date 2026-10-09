#include "nano_frame.h"

bool nano_parse_frame_header(const uint8_t *pkt, size_t len, nano_frame_header_t *out)
{
    if (len < 2) return false;
    uint16_t raw = (uint16_t)(pkt[0] | (pkt[1] << 8));
    uint16_t body = raw & NANO_LENGTH_MASK;
    if (body != len - 2) return false;
    if (out) {
        out->body_length = body;
        out->start = (raw & NANO_FLAG_START) != 0;
        out->end = (raw & NANO_FLAG_END) != 0;
    }
    return true;
}

size_t nano_split_trailer(const uint8_t *body, size_t len, int *msg_type)
{
    if (len >= 4 && body[len - 1] == 0 && body[len - 2] == 0 && body[len - 3] == 0) {
        if (msg_type) *msg_type = body[len - 4];
        return len - 4;
    }
    if (msg_type) *msg_type = -1;
    return len;
}

bool nano_is_tuner_pitch_packet(const uint8_t *pkt, size_t len)
{
    return len >= 8 && pkt[1] == 0xC0 && pkt[len - 4] == 0x80 && pkt[len - 3] == 0 && pkt[len - 2] == 0 && pkt[len - 1] == 0;
}

void nano_frame_begin(nano_pb_writer_t *w, uint8_t *out, size_t cap)
{
    nano_pb_init(w, out, cap);
    const uint8_t header[2] = { 0, 0 }; /* filled in by nano_frame_end */
    nano_pb_raw(w, header, sizeof(header));
}

size_t nano_frame_end(nano_pb_writer_t *w, uint8_t msg_type)
{
    const uint8_t trailer[4] = { msg_type, 0, 0, 0 };
    nano_pb_raw(w, trailer, sizeof(trailer));
    size_t body = w->pos - 2;
    if (!w->ok || body > NANO_LENGTH_MASK) return 0;
    uint16_t header = (uint16_t)(body | NANO_FLAG_START | NANO_FLAG_END);
    w->buf[0] = (uint8_t)header;
    w->buf[1] = (uint8_t)(header >> 8);
    return w->pos;
}
