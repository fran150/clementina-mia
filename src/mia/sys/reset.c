#include "sys/reset.h"

#include <stdbool.h>
#include <stdint.h>

#include "pico/stdlib.h"

#include "hardware/gpio_mapping.h"

#define MIA_CPU_RESET_PULSE_CYCLES 4

static bool reset_request_asserted = false;

void mia_reset_runtime_state(void);

static inline bool mia_reset_request_active(void) {
    return !gpio_get(MIA_RESETB_PIN);
}

static inline void mia_drive_cpu_reset(bool asserted) {
    gpio_put(CPU_RESB_PIN, !asserted);
}

void mia_prepare_reset_lines(void) {
    gpio_init(CPU_RESB_PIN);
    gpio_put(CPU_RESB_PIN, false);
    gpio_set_dir(CPU_RESB_PIN, GPIO_OUT);

    gpio_init(MIA_RESETB_PIN);
    gpio_set_dir(MIA_RESETB_PIN, GPIO_IN);
    gpio_pull_up(MIA_RESETB_PIN);
}

static void mia_wait_phi2_cycles(uint32_t cycles) {
    for (uint32_t i = 0; i < cycles; i++) {
        while (!gpio_get(CPU_PHI2_PIN)) {
            tight_loop_contents();
        }
        while (gpio_get(CPU_PHI2_PIN)) {
            tight_loop_contents();
        }
    }
}

void mia_handle_reset_request(void) {
    bool requested = mia_reset_request_active();

    if (requested && !reset_request_asserted) {
        reset_request_asserted = true;
        mia_drive_cpu_reset(true);
        mia_reset_runtime_state();
    }

    if (requested) {
        mia_drive_cpu_reset(true);
        return;
    }

    if (reset_request_asserted) {
        mia_wait_phi2_cycles(MIA_CPU_RESET_PULSE_CYCLES);
        mia_drive_cpu_reset(false);
        reset_request_asserted = false;
    }
}

void mia_pulse_cpu_reset(void) {
    mia_drive_cpu_reset(true);
    mia_wait_phi2_cycles(MIA_CPU_RESET_PULSE_CYCLES);

    if (!mia_reset_request_active()) {
        mia_drive_cpu_reset(false);
    }
}
