#ifndef _MIA_SYS_SPEED_H_
#define _MIA_SYS_SPEED_H_

#include <stdbool.h>
#include <stdint.h>

#include "hardware/pio.h"
#include "pico.h"

#include "etc/status.h"

#define MIA_DEFAULT_PHI2_HZ 2000u

extern volatile uint32_t mia_staged_phi2_hz;
extern volatile uint32_t mia_requested_phi2_hz;
extern volatile uint32_t mia_applied_phi2_hz;
extern volatile bool mia_speed_change_requested;

static inline __force_inline uint8_t mia_speed_get_applied_byte(uint8_t byte) {
    return (mia_applied_phi2_hz >> (byte * 8)) & 0xFF;
}

static inline __force_inline void mia_speed_stage_byte(uint8_t byte, uint8_t value) {
    uint32_t shift = byte * 8;
    uint32_t mask = 0xFFu << shift;
    mia_staged_phi2_hz = (mia_staged_phi2_hz & ~mask) | ((uint32_t)value << shift);
}

static inline __force_inline void mia_speed_commit(void) {
    mia_requested_phi2_hz = mia_staged_phi2_hz;
    mia_status_set_flag(MIA_STAT_SPEED_CHANGING);
    mia_speed_change_requested = true;
}

void mia_speed_configure_phi2(pio_sm_config *config, uint32_t target_hz);
void mia_speed_apply_current(void);
void mia_speed_reset_runtime_state(void);
void mia_speed_service(void);

#endif
