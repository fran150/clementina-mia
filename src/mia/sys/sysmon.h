#ifndef _MIA_SYS_SYSMON_H_
#define _MIA_SYS_SYSMON_H_

#include <stdint.h>

// Resource monitor behind `status sys` (see docs/diagnostics.md). Everything
// here runs on core 0. Core 1's action loop is not touched, except in the
// diagnostic build (make diag), where it also counts the cycles it spends on
// bus events (see mia_core1_load_read in mia.h).

// Where core 0's main loop spends its time. Each lap charges the cycles since
// the previous lap to one slot.
typedef enum {
    SYSMON_MIA,      // reset requests, mode switch, errors, speed, input
    SYSMON_SD,       // SD card and file requests
    SYSMON_WIFI,     // cyw43_arch_poll: Wi-Fi driver and lwIP
    SYSMON_VIDEO,    // frames and packets to the video client
    SYSMON_CONSOLE,  // console input and commands
    SYSMON_OTHER,    // LED blink and the loop itself
    SYSMON_SLOTS
} sysmon_slot_t;

// Call first thing in main(), before mia_init() launches core 1: starts the
// cycle counter and fills both stacks with a pattern for the high-water marks.
void sysmon_init(void);

// Charge the time since the previous lap to slot.
void sysmon_lap(sysmon_slot_t slot);

// Call at the end of each main loop pass: charges the rest of the pass to
// SYSMON_OTHER, keeps the slowest pass, samples the action FIFO and closes
// the one-second window.
void sysmon_pass_end(void);

// status sys [clear]
void sysmon_print(void);
void sysmon_clear(void);

#endif
