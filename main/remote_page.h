/*
 * A remote page: settings the state dumps do not carry, read from the pedal while their page shows (the IR tab
 * now; FX parameters or capture tone next). The helper does the bookkeeping every such page needs:
 *   - read on open, and again when the preset or the page's key (e.g. the IR slot) changes, or after the pedal
 *     reverted edits (EXIT);
 *   - match each reply to the read it answers (replies come back in request order): the answer knows its preset
 *     and key, and whether it was asked after our last write ("fresh": then the pedal's word stands over a control
 *     still holding its own value);
 *   - writes: sent, timed, optionally confirmed by a read;
 *   - hand each answer to the screen (UI_PAGES), retried while the display is busy.
 * A page supplies its ops and a buffer for the decoded answer; see ir_page.c.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    const char *name;                  /* for the log */
    int reply_type;                    /* trailer type of the pedal's answer */
    /* What the page describes besides the preset (the IR slot, 0 = off), checked against every state. */
    int (*key)(void);
    /* The read for the current state; 0 = nothing to read now (the page shows why, e.g. "IR is off"). */
    size_t (*build_read)(uint8_t *out, size_t cap);
    /* Payload (no trailer) into `data`; false = the answer describes nothing. Also logs it. */
    bool (*decode)(const uint8_t *payload, size_t len, void *data, int preset, int key);
    /* Under the LVGL lock: the answer (NULL = none) for `preset`; `fresh` = asked after our last write. */
    void (*show)(const void *data, int preset, bool fresh);
} remote_page_ops_t;

typedef struct {
    int preset, key;
    int64_t sent_us;
} remote_read_t;

#define REMOTE_READS_MAX 4

typedef struct {
    const remote_page_ops_t *ops;
    void *data;                        /* the last answer (ops->decode fills it) */
    bool open;
    remote_read_t reads[REMOTE_READS_MAX]; /* sent, unanswered; oldest first */
    int reads_n;
    remote_read_t asked;               /* the latest read sent (or skipped: nothing to read) */
    remote_read_t answered;            /* the read the last answer belongs to */
    bool valid, fresh, shown;          /* shown: the last answer reached the screen */
    int64_t written_us;                /* our last write */
} remote_page_t;

/* Pages answer replies and get state / link / screen calls once registered (at start). */
void remote_page_register(remote_page_t *page);
void remote_page_set_open(remote_page_t *page, bool open);
/* After our write of `frame`: timed for "fresh"; with `confirm`, read back right away. */
bool remote_page_write(remote_page_t *page, const uint8_t *frame, size_t n, bool confirm);
/* The last answer when it describes what is shown now (this preset and key), else NULL. */
const void *remote_page_current(const remote_page_t *page);

/* For every registered page. */
bool remote_pages_on_reply(int msg_type, const uint8_t *payload, size_t len); /* true = a page's answer */
void remote_pages_on_state(void);
void remote_pages_on_revert(void); /* unsaved edits flipped: the next state reads again */
void remote_pages_link_reset(void);
void remote_pages_ui_push(uint32_t parts);
