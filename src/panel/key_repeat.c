#include <string.h>
#include "panel/key_repeat.h"
void key_repeat_init(key_repeat_t *s) { memset(s, 0, sizeof(*s)); }
int key_repeat_step(key_repeat_t *s, uint32_t now, unsigned int mask,
                    uint32_t context, int allowed)
{
    uint32_t interval;
    if (!s) return 0;
    if (!s->initialized) { s->context = context; s->initialized = 1; }
    if (context != s->context || !allowed || mask > 2U) {
        s->context = context;
        s->blocked = mask != 0U;
        s->mask = 0;
        s->started = 0;
        return 0;
    }
    if (!mask) { s->mask = s->blocked = s->started = 0; return 0; }
    if (s->blocked) return 0;
    if (mask != s->mask) {
        s->mask = mask;
        s->pressed_at = s->emitted_at = now;
        s->started = 0;
        return mask == 1U ? -1 : 1;
    }
    /* 500ms initial delay, then 100ms; strictly after 5s -> 10ms.
     * Unsigned differences also support the 32-bit clock wrap. */
    interval = s->started ? (now - s->pressed_at > 5000U ? 10U : 100U) : 500U;
    if (now - s->emitted_at < interval) return 0;
    s->emitted_at = now; /* no catch-up queue */
    s->started = 1;
    return mask == 1U ? -1 : 1;
}
