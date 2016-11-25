
#include <stdint.h>

extern int
timer_initialize(void);

extern uint64_t
timer_current(void);

extern uint64_t
timer_ticks_per_second(void);

extern double
timer_ticks_to_seconds(uint64_t ticks);
