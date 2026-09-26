/*
 * START ... END fragment assembly for the c305 stream, over a caller-owned
 * buffer (the metadata dump is ~17 KB, so give it 20 KB). Port of
 * NanoGig `MessageAssembler`. Time is passed in by the caller so the core
 * stays host-testable; call nano_assembler_tick() periodically for the
 * inactivity fallback (2.5 s of silence flushes an unterminated message).
 */
#ifndef NANO_ASSEMBLER_H
#define NANO_ASSEMBLER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NANO_ASSEMBLER_INACTIVITY_MS 2500

typedef void (*nano_message_cb)(void *ctx, const uint8_t *body, size_t len, int packets, bool complete);

typedef struct {
    uint8_t *buf;
    size_t cap;
    size_t len;
    int packets;
    bool overflow;
    uint32_t last_ms;
    nano_message_cb on_message;
    void *ctx;
} nano_assembler_t;

void nano_assembler_init(nano_assembler_t *a, uint8_t *buf, size_t cap, nano_message_cb cb, void *ctx);
/*
 * Feed one notification packet. A single-packet message (START+END) is emitted
 * at once; a packet whose header does not validate is emitted as-is (legacy
 * heuristics are deliberately not ported: every 2.2.1 packet validates).
 */
void nano_assembler_push(nano_assembler_t *a, const uint8_t *pkt, size_t len, uint32_t now_ms);
/* Flush a stalled partial message after the inactivity window. */
void nano_assembler_tick(nano_assembler_t *a, uint32_t now_ms);
void nano_assembler_reset(nano_assembler_t *a);
static inline bool nano_assembler_open(const nano_assembler_t *a) { return a->packets > 0; }

#ifdef __cplusplus
}
#endif
#endif
