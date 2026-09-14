#include "sys/timing.h"
#include "mem/mem.h"
#include "pico/time.h"

// Core 0 owns these values. Only explicit snapshot commands publish to RAM,
// so the 6502 can read all bytes without tearing, even at a very slow PHI2.
static uint64_t clock_epoch_us, ti_epoch_us;
static uint32_t ti_base_ticks;
void mia_timing_reset(void) {
    clock_epoch_us = ti_epoch_us = time_us_64();
    ti_base_ticks = 0;
}
void mia_timing_set_ti(uint32_t ticks) {
    if (ticks >= MIA_CLOCK_TICKS_PER_DAY) return;
    ti_base_ticks = ticks;
    ti_epoch_us = time_us_64();
}
void mia_timing_snapshot(void) {
    uint64_t now = time_us_64();
    uint32_t ms = (uint32_t)((now - clock_epoch_us) / 1000u);
    uint64_t elapsed = now - ti_epoch_us;
    uint32_t ti = (uint32_t)((ti_base_ticks + (elapsed / 1000000u) * 60u +
        ((elapsed % 1000000u) * 60u) / 1000000u) % MIA_CLOCK_TICKS_PER_DAY);
    uint8_t *p = &mem[MIA_CLOCK_SNAPSHOT_OFFSET];
    for (unsigned i = 0; i < 4; ++i) p[i] = (uint8_t)(ms >> (8*i));
    for (unsigned i = 0; i < 3; ++i) p[4+i] = (uint8_t)(ti >> (8*i));
    p[7] = 1; // protocol version, committed before command completion
}
