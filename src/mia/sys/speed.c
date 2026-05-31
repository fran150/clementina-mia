#include "sys/speed.h"

#include "hardware/clocks.h"

#include "hardware/pio_mapping.h"
#include "irq/irq.h"

#define MIA_PHI2_CYCLES_PER_PERIOD 1590u

volatile uint32_t mia_staged_phi2_hz = MIA_DEFAULT_PHI2_HZ;
volatile uint32_t mia_requested_phi2_hz = MIA_DEFAULT_PHI2_HZ;
volatile uint32_t mia_applied_phi2_hz = MIA_DEFAULT_PHI2_HZ;
volatile bool mia_speed_change_requested = false;

static uint32_t mia_min_phi2_frequency(void) {
    uint32_t system_freq = clock_get_hz(clk_sys);
    uint32_t max_divider_cycles = 65536u * MIA_PHI2_CYCLES_PER_PERIOD;
    return (system_freq + max_divider_cycles - 1) / max_divider_cycles;
}

static uint32_t mia_max_phi2_frequency(void) {
    return clock_get_hz(clk_sys) / MIA_PHI2_CYCLES_PER_PERIOD;
}

static uint32_t mia_clamp_phi2_frequency(uint32_t target_hz) {
    uint32_t min_hz = mia_min_phi2_frequency();
    uint32_t max_hz = mia_max_phi2_frequency();

    if (target_hz < min_hz) {
        return min_hz;
    }

    if (target_hz > max_hz) {
        return max_hz;
    }

    return target_hz;
}

static float mia_phi2_divider(uint32_t target_hz) {
    float pio_freq = target_hz * (float)MIA_PHI2_CYCLES_PER_PERIOD;
    float system_freq = (float)clock_get_hz(clk_sys);
    float div = system_freq / pio_freq;

    if (div < 1.0f) div = 1.0f;
    if (div > 65536.0f) div = 65536.0f;

    return div;
}

void mia_speed_configure_phi2(pio_sm_config *config, uint32_t target_hz) {
    target_hz = mia_clamp_phi2_frequency(target_hz);
    sm_config_set_clkdiv(config, mia_phi2_divider(target_hz));
}

static void mia_speed_apply_phi2(uint32_t target_hz) {
    target_hz = mia_clamp_phi2_frequency(target_hz);
    float div = mia_phi2_divider(target_hz);

    pio_sm_set_clkdiv(MIA_WRITE_PIO, MIA_WRITE_SM, div);
    pio_sm_set_clkdiv(MIA_READ_PIO, MIA_READ_SM, div);
    pio_sm_set_clkdiv(MIA_ACT_PIO, MIA_ACT_SM, div);

    mia_applied_phi2_hz = target_hz;
}

void mia_speed_apply_current(void) {
    mia_speed_apply_phi2(mia_applied_phi2_hz);
}

void mia_speed_reset_runtime_state(void) {
    mia_staged_phi2_hz = mia_applied_phi2_hz;
    mia_requested_phi2_hz = mia_applied_phi2_hz;
    mia_speed_change_requested = false;
    mia_status_clear_flag(MIA_STAT_SPEED_CHANGING);
}

void mia_speed_service(void) {
    if (!mia_speed_change_requested) {
        return;
    }

    uint32_t requested_hz = mia_requested_phi2_hz;
    mia_speed_change_requested = false;

    mia_speed_apply_phi2(requested_hz);
    mia_staged_phi2_hz = mia_applied_phi2_hz;
    mia_requested_phi2_hz = mia_applied_phi2_hz;
    mia_status_clear_flag(MIA_STAT_SPEED_CHANGING);
    mia_irq_set_flag(IRQ_SPEED_CHANGED);
}
