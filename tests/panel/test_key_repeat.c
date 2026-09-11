#include <assert.h>
#include <stdio.h>
#include "panel/key_repeat.h"
int main(void)
{
    key_repeat_t s;
    unsigned int i, count = 0, value = 502, octet = 255, list = 0, seen = 0;
    key_repeat_init(&s);
    assert(key_repeat_step(&s, 0, 2, 1, 1) == 1);
    assert(!key_repeat_step(&s, 499, 2, 1, 1));
    assert(key_repeat_step(&s, 500, 2, 1, 1) == 1);
    for (i = 501; i <= 5000; ++i) count += key_repeat_step(&s, i, 2, 1, 1);
    assert(count == 45);
    assert(!key_repeat_step(&s, 5001, 2, 1, 1));
    assert(key_repeat_step(&s, 5010, 2, 1, 1) == 1);
    assert(!key_repeat_step(&s, 5011, 0, 1, 1));
    assert(!key_repeat_step(&s, 9000, 0, 1, 1));
    assert(key_repeat_step(&s, 9001, 1, 1, 1) == -1);
    assert(!key_repeat_step(&s, 9010, 1, 1, 1));
    assert(key_repeat_step(&s, 9501, 1, 1, 1) == -1);
    assert(key_repeat_step(&s, 20000, 1, 1, 1) == -1);
    assert(!key_repeat_step(&s, 20000, 1, 1, 1));
    assert(!key_repeat_step(&s, 20001, 1, 2, 1));
    assert(!key_repeat_step(&s, 26000, 1, 2, 1));
    key_repeat_step(&s, 26001, 0, 2, 1);
    assert(key_repeat_step(&s, 26002, 2, 2, 1) == 1);
    assert(key_repeat_step(&s, 26003, 1, 2, 1) == -1);
    assert(!key_repeat_step(&s, 26013, 1, 2, 1));
    assert(!key_repeat_step(&s, 27000, 3, 2, 1));
    assert(!key_repeat_step(&s, 28000, 2, 2, 1));
    key_repeat_step(&s, 28001, 0, 2, 1);
    assert(!key_repeat_step(&s, 29000, 2, 2, 0));
    assert(!key_repeat_step(&s, 39000, 2, 2, 0));
    key_repeat_init(&s);
    assert(key_repeat_step(&s, UINT32_MAX - 200U, 1, 0, 1) == -1);
    assert(key_repeat_step(&s, 299, 1, 0, 1) == -1);
    key_repeat_init(&s);
    for (i = 0; value < 1502; ++i) if (key_repeat_step(&s, i, 2, 0, 1)) ++value;
    assert(value == 1502 && i < 15000);
    key_repeat_step(&s, i++, 0, 0, 1);
    while (value > 502) if (key_repeat_step(&s, i++, 1, 0, 1)) --value;
    key_repeat_init(&s);
    for (i = 0; i < 6000; ++i) if (key_repeat_step(&s, i, 2, 0, 1)) {
        octet = (octet + 1) % 256;
        list = (list + 1) % 5;
        seen |= 1U << list;
    }
    assert(octet < 256 && seen == 31);
    puts("key-repeat: timing, release, direction, context, commands, wrap, ports, lists PASS");
    return 0;
}
