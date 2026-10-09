#include "remote_page.h"

#include <string.h>

#include "app.h"
#include "esp_log.h"

static const char *TAG = "page";

#define PAGES_MAX 4
static remote_page_t *s_pages[PAGES_MAX];
static int s_page_count;

void remote_page_register(remote_page_t *page)
{
    if (s_page_count < PAGES_MAX) s_pages[s_page_count++] = page;
    page->asked = page->answered = (remote_read_t){ -1, -1, 0 };
}

static remote_read_t reads_pop(remote_page_t *p)
{
    remote_read_t first = p->reads[0];
    p->reads_n--;
    memmove(p->reads, p->reads + 1, sizeof(p->reads[0]) * (size_t)p->reads_n);
    return first;
}

static void request(remote_page_t *p)
{
    if (!g_app.link_ready || !g_app.state_valid) return;
    p->asked = (remote_read_t){ g_app.state.active_preset, p->ops->key(), app_now_us() };
    uint8_t f[NANO_FRAME_MAX];
    size_t n = p->ops->build_read(f, sizeof(f));
    if (!n) return; /* nothing to read: the page shows why */
    if (!app_send(f, n)) {
        p->asked.preset = -1; /* not sent: the next state asks again */
        return;
    }
    if (p->reads_n == REMOTE_READS_MAX) reads_pop(p); /* never answered: forget the oldest */
    p->reads[p->reads_n++] = p->asked;
    ESP_LOGI(TAG, "-> %s read (preset %d, key %d)", p->ops->name, p->asked.preset + 1, p->asked.key);
}

void remote_page_set_open(remote_page_t *p, bool open)
{
    p->open = open;
    if (open) request(p);
}

bool remote_page_write(remote_page_t *p, const uint8_t *frame, size_t n, bool confirm)
{
    if (!app_send(frame, n)) return false;
    p->written_us = app_now_us();
    if (confirm) request(p);
    return true;
}

const void *remote_page_current(const remote_page_t *p)
{
    if (!p->valid || p->answered.preset != g_app.state.active_preset || p->answered.key != p->ops->key()) return NULL;
    return p->data;
}

bool remote_pages_on_reply(int msg_type, const uint8_t *payload, size_t len)
{
    for (int i = 0; i < s_page_count; i++) {
        remote_page_t *p = s_pages[i];
        if (p->ops->reply_type != msg_type) continue;
        /* Unasked: as old as anything we know. */
        p->answered = p->reads_n > 0 ? reads_pop(p) : (remote_read_t){ g_app.state.active_preset, p->ops->key(), 0 };
        p->valid = p->ops->decode(payload, len, p->data, p->answered.preset, p->answered.key);
        p->fresh = p->answered.sent_us > p->written_us;
        p->shown = false; /* shown either way: no answer replaces what the page showed before */
        ui_mark(UI_PAGES);
        return true;
    }
    return false;
}

void remote_pages_on_state(void)
{
    for (int i = 0; i < s_page_count; i++) {
        remote_page_t *p = s_pages[i];
        if (p->open && (g_app.state.active_preset != p->asked.preset || p->ops->key() != p->asked.key)) request(p);
    }
}

void remote_pages_on_revert(void)
{
    for (int i = 0; i < s_page_count; i++) {
        if (s_pages[i]->open) s_pages[i]->asked.key = -1; /* the state read that follows decides what to ask */
    }
}

void remote_pages_link_reset(void)
{
    for (int i = 0; i < s_page_count; i++) {
        remote_page_t *p = s_pages[i];
        p->reads_n = 0;
        p->asked = (remote_read_t){ -1, -1, 0 };
        p->valid = false;
    }
}

void remote_pages_ui_push(uint32_t parts)
{
    if (!(parts & UI_PAGES)) return;
    for (int i = 0; i < s_page_count; i++) {
        remote_page_t *p = s_pages[i];
        if (p->shown) continue;
        p->shown = true;
        if (p->open) p->ops->show(p->valid ? p->data : NULL, p->answered.preset, p->fresh);
    }
}
