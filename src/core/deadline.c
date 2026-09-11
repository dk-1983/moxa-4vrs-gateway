#include "core/deadline.h"

#define CORE_TICK_HALF_RANGE 0x80000000U

int core_ticks_valid_interval(core_tick_t interval)
{
    return interval < CORE_TICK_HALF_RANGE;
}

core_deadline_t core_deadline_after(core_tick_t now, core_tick_t interval)
{
    core_deadline_t result;
    result.at = now + interval;
    result.armed = core_ticks_valid_interval(interval);
    return result;
}

int core_deadline_expired(core_deadline_t deadline, core_tick_t now)
{
    if (!deadline.armed)
        return 0;
    return now - deadline.at < CORE_TICK_HALF_RANGE;
}

core_tick_t core_elapsed(core_tick_t since, core_tick_t now)
{
    return now - since;
}
