#include "nano_assembler.h"
#include "nano_frame.h"

#include <string.h>

void nano_assembler_init(nano_assembler_t *a, uint8_t *buf, size_t cap, nano_message_cb cb, void *ctx)
{
    memset(a, 0, sizeof(*a));
    a->buf = buf;
    a->cap = cap;
    a->on_message = cb;
    a->ctx = ctx;
}

static void flush(nano_assembler_t *a, bool complete)
{
    if (a->packets == 0) return;
    size_t len = a->len;
    int packets = a->packets;
    bool overflow = a->overflow;
    a->len = 0;
    a->packets = 0;
    a->overflow = false;
    if (len == 0 || overflow) return; /* a truncated dump would decode garbage: drop it */
    a->on_message(a->ctx, a->buf, len, packets, complete);
}

void nano_assembler_push(nano_assembler_t *a, const uint8_t *pkt, size_t len, uint32_t now_ms)
{
    nano_frame_header_t h;
    bool valid = nano_parse_frame_header(pkt, len, &h);
    if (!valid) {
        /* Not our framing: hand it over untouched, do not disturb an open message. */
        if (len) a->on_message(a->ctx, pkt, len, 1, true);
        return;
    }
    if (h.start && nano_assembler_open(a)) flush(a, false);
    size_t body = len - 2;
    if (a->len + body > a->cap) {
        a->overflow = true;
    } else {
        memcpy(a->buf + a->len, pkt + 2, body);
        a->len += body;
    }
    a->packets++;
    a->last_ms = now_ms;
    if (h.end) flush(a, true);
}

void nano_assembler_tick(nano_assembler_t *a, uint32_t now_ms)
{
    if (nano_assembler_open(a) && (uint32_t)(now_ms - a->last_ms) >= NANO_ASSEMBLER_INACTIVITY_MS) flush(a, false);
}

void nano_assembler_reset(nano_assembler_t *a)
{
    a->len = 0;
    a->packets = 0;
    a->overflow = false;
}
