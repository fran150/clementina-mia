#include "sys/reset.h"

#include <stdbool.h>
#include <stdint.h>

#include "pico/stdlib.h"

#include "hardware/gpio_mapping.h"
#include "sys/speed.h"

#define MIA_CPU_RESET_PULSE_CYCLES 4

static bool reset_request_asserted = false;
static bool reset_release_pending = false;
static absolute_time_t reset_release_time;

void mia_reset_runtime_state(void);

// Returns true when the external active-low MIA reset request is asserted.
static inline bool mia_reset_request_active(void) {
    return !gpio_get(MIA_RESETB_PIN);
}

// Sets the 6502 reset line. The physical RESB signal is active-low.
void mia_set_cpu_reset(bool asserted) {
    gpio_put(CPU_RESB_PIN, !asserted);
}

// Initializes the CPU reset output and the external MIA reset request input.
void mia_prepare_reset_lines(void) {
    gpio_init(CPU_RESB_PIN);
    gpio_put(CPU_RESB_PIN, false);
    gpio_set_dir(CPU_RESB_PIN, GPIO_OUT);

    gpio_init(MIA_RESETB_PIN);
    gpio_set_dir(MIA_RESETB_PIN, GPIO_IN);
    gpio_pull_up(MIA_RESETB_PIN);
}

// Calculates how long reset must stay asserted for the requested number of PHI2 cycles.
// This follows the RP6502 strategy of using the configured/applied clock instead of
// blocking while polling the PHI2 pin.
static uint32_t mia_reset_hold_us(void) {
    uint32_t phi2_hz = mia_applied_phi2_hz;
    if (phi2_hz == 0) {
        phi2_hz = MIA_DEFAULT_PHI2_HZ;
    }

    return (MIA_CPU_RESET_PULSE_CYCLES * 1000000u + phi2_hz - 1) / phi2_hz;
}

// Starts a pending reset release. The main loop will release RESB after the timer expires.
void mia_schedule_cpu_reset_release(void) {
    reset_release_pending = true;
    reset_release_time = delayed_by_us(get_absolute_time(), mia_reset_hold_us());
}

// Releases reset only after the hold time expires and no external reset request is active.
static void mia_service_cpu_reset_release(void) {
    if (!reset_release_pending || mia_reset_request_active()) {
        return;
    }

    absolute_time_t now = get_absolute_time();
    if (absolute_time_diff_us(now, reset_release_time) < 0) {
        mia_set_cpu_reset(false);
        reset_release_pending = false;
    }
}

void mia_handle_reset_request(void) {
    bool requested = mia_reset_request_active();

    // A newly asserted external reset request immediately holds the CPU in reset
    // and resets MIA runtime state back into loader mode.
    if (requested && !reset_request_asserted) {
        reset_request_asserted = true;
        reset_release_pending = false;
        mia_set_cpu_reset(true);
        mia_reset_runtime_state();
    }

    if (requested) {
        mia_set_cpu_reset(true);
        return;
    }

    // When the external reset request is released, keep RESB low for the reset hold time.
    if (reset_request_asserted) {
        reset_request_asserted = false;
        mia_schedule_cpu_reset_release();
    }

    mia_service_cpu_reset_release();
}

// Pulses reset by asserting RESB now and scheduling the later release.
void mia_pulse_cpu_reset(void) {
    mia_set_cpu_reset(true);
    mia_schedule_cpu_reset_release();
}
