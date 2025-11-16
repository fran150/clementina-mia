/**
 * Clock Control System for MIA
 * Manages PWM-based clock generation for Clementina 6502 system
 */

#ifndef CLOCK_CONTROL_H
#define CLOCK_CONTROL_H

#include <stdint.h>
#include <stdbool.h>

// Clock frequencies
#define CLOCK_FREQ_BOOT     100000   // 100 kHz for boot phase
#define CLOCK_FREQ_NORMAL   1000000  // 1 MHz for normal operation

typedef enum {
    CLOCK_PHASE_BOOT,
    CLOCK_PHASE_NORMAL
} clock_phase_t;

void clock_control_init(void);
void clock_control_set_phase(clock_phase_t phase);
void clock_control_reset(void);

#endif // CLOCK_CONTROL_H