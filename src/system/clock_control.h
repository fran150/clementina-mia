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

// Clock stability requirement
#define CLOCK_DEVIATION_MAX 0.001f   // 0.1% maximum deviation

typedef enum {
    CLOCK_PHASE_BOOT,
    CLOCK_PHASE_NORMAL
} clock_phase_t;

// Function prototypes
void clock_control_init(void);
void clock_control_set_phase(clock_phase_t phase);
void clock_control_set_frequency(uint32_t frequency_hz);
uint32_t clock_control_get_frequency(void);
clock_phase_t clock_control_get_phase(void);
bool clock_control_is_stable(void);
float clock_control_get_deviation(void);
bool clock_control_validate_frequency(uint32_t frequency_hz);
void clock_control_enable(bool enable);
bool clock_control_is_enabled(void);
void clock_control_reset(void);

#endif // CLOCK_CONTROL_H