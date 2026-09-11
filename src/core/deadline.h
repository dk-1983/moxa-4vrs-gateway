#ifndef FOURVRS_CORE_DEADLINE_H
#define FOURVRS_CORE_DEADLINE_H

typedef unsigned int core_tick_t;

typedef struct core_deadline {
    core_tick_t at;
    int armed;
} core_deadline_t;

int core_ticks_valid_interval(core_tick_t interval);
core_deadline_t core_deadline_after(core_tick_t now, core_tick_t interval);
int core_deadline_expired(core_deadline_t deadline, core_tick_t now);
core_tick_t core_elapsed(core_tick_t since, core_tick_t now);

#endif
