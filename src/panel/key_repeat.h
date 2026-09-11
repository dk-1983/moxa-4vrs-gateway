#ifndef FOURVRS_KEY_REPEAT_H
#define FOURVRS_KEY_REPEAT_H
#include <stdint.h>
/* Explicit physical state only. Queue silence is NOT a release/held event.
 * Mask: F2=1, F4=2. Output -1/+1 is a single existing editor/list step.
 * Context identifies screen AND edited field, but not list selection/value.
 * Caller must sample at least every 10ms to attain the proposed fast rate. */
typedef struct key_repeat {
    uint32_t pressed_at, emitted_at, context;
    unsigned int mask, blocked, started, initialized;
} key_repeat_t;
void key_repeat_init(key_repeat_t *state);
int key_repeat_step(key_repeat_t *state, uint32_t monotonic_ms,
                    unsigned int physical_mask, uint32_t context,
                    int repeat_allowed);
#endif
