#ifndef MIA_SYS_TIMING_H
#define MIA_SYS_TIMING_H
#include <stdint.h>
#define MIA_CLOCK_SNAPSHOT_OFFSET 0x11078u
#define MIA_CLOCK_TICKS_PER_DAY 5184000u
#define MIA_CMD_CLOCK_SNAPSHOT 0x55u
#define MIA_CMD_CLOCK_SET_TI 0x56u
void mia_timing_reset(void);
void mia_timing_snapshot(void);
void mia_timing_set_ti(uint32_t ticks);
#endif
