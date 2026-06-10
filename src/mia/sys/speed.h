#ifndef _MIA_SYS_SPEED_H_
#define _MIA_SYS_SPEED_H_

#include <stdbool.h>
#include <stdint.h>

#include "hardware/pio.h"
#include "pico.h"

#include "etc/status.h"

// Default PHI2 speed requested when MIA starts.
// The speed module may change clk_sys or clamp this to the closest supported value.

// #define MIA_DEFAULT_PHI2_HZ 1000000u  // MIA default for real hardware.
#define MIA_DEFAULT_PHI2_HZ 3000u          // Temporary emulator bring-up speed.

// Public bounds for requested PHI2 speeds through the SPEED_L/M/H config registers.
// Values outside this range are clamped before applying the clock.
#define MIA_MIN_PHI2_HZ 1u
#define MIA_MAX_PHI2_HZ 8000000u

// Speed currently staged by writes to SPEED_L/M/H.
// It becomes requested when SPEED_H is written.
extern volatile uint32_t mia_staged_phi2_hz;

// Speed pending application by mia_speed_service().
extern volatile uint32_t mia_requested_phi2_hz;

// Speed actually applied after clamping and clock/divider selection.
extern volatile uint32_t mia_applied_phi2_hz;

// Set when the 6502 commits a speed change through the config registers.
extern volatile bool mia_speed_change_requested;

// Returns one byte of the applied PHI2 speed.
// This is used by the config register read path.
static inline __force_inline uint8_t mia_speed_get_applied_byte(uint8_t byte) {
    return (mia_applied_phi2_hz >> (byte * 8)) & 0xFF;
}

// Stages one byte of the requested PHI2 speed.
// The value is not applied until the high byte is committed.
static inline __force_inline void mia_speed_stage_byte(uint8_t byte, uint8_t value) {
    uint32_t shift = byte * 8;
    uint32_t mask = 0xFFu << shift;
    mia_staged_phi2_hz = (mia_staged_phi2_hz & ~mask) | ((uint32_t)value << shift);
}

// Commits the staged PHI2 speed and marks the change for the main service loop.
// The actual clock change is deferred so register writes stay quick.
static inline __force_inline void mia_speed_commit(void) {
    mia_requested_phi2_hz = mia_staged_phi2_hz;
    mia_status_set_flag(MIA_STAT_SPEED_CHANGING);
    mia_speed_change_requested = true;
}

// Sets the initial PIO clock divider before a state machine is enabled.
void mia_speed_configure_phi2(pio_sm_config *config, uint32_t target_hz);

// Re-applies the current PHI2 speed to all bus timing state machines.
void mia_speed_apply_current(void);

// Resets speed bookkeeping after a MIA runtime reset.
void mia_speed_reset_runtime_state(void);

// Applies a pending speed change requested through SPEED_L/M/H.
void mia_speed_service(void);

#endif
