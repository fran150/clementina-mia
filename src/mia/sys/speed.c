#include "sys/speed.h"

#include "hardware/clocks.h"
#include "pico/cyw43_driver.h"

#include "hardware/pio_mapping.h"
#include "irq/irq.h"

// The write PIO program generates one complete PHI2 period every 32 PIO cycles.
// This is the timing contract shared by the read/write/action state machines.
#define MIA_PHI2_PIO_CYCLES_PER_PERIOD 32u

// Slow requests below the normal divider range use this PLL clock unless the
// requested speed is low enough to derive clk_sys directly from the crystal.
#define MIA_SLOW_SYS_CLOCK_HZ 18000000u

// Requests above the normal divider range use this clock. 256 MHz allows PHI2
// up to 8 MHz with the unchanged 32-cycle PIO timing program.
#define MIA_FAST_SYS_CLOCK_HZ 256000000u

// CYW43 talks to the Pico through a PIO SPI link, so changing clk_sys changes
// the Wi-Fi chip link clock too. These divisors keep that link in a sane range.
#define MIA_CYW43_SLOW_CLKDIV_INT 1u
#define MIA_CYW43_NORMAL_CLKDIV_INT 2u
#define MIA_CYW43_FAST_CLKDIV_INT 4u
#define MIA_CYW43_CLKDIV_FRAC8 0u

volatile uint32_t mia_staged_phi2_hz = MIA_DEFAULT_PHI2_HZ;
volatile uint32_t mia_requested_phi2_hz = MIA_DEFAULT_PHI2_HZ;
volatile uint32_t mia_applied_phi2_hz = MIA_DEFAULT_PHI2_HZ;
volatile bool mia_speed_change_requested = false;

// Normal system clock captured before MIA slows down or overclocks the Pico.
// This lets the speed code return to the boot clock for ordinary PHI2 requests.
static uint32_t mia_normal_sys_clock_hz = 0;

// Maximum PIO divider expressed as PIO cycles per PHI2 period.
// This determines the slowest PHI2 that a given clk_sys can generate.
static uint32_t mia_max_divider_cycles(void) {
    return 65536u * MIA_PHI2_PIO_CYCLES_PER_PERIOD;
}

// Returns the boot-time clk_sys value.
// The first call must happen before any speed reclock changes clk_sys.
static uint32_t mia_normal_sys_clock_frequency(void) {
    if (mia_normal_sys_clock_hz == 0) {
        mia_normal_sys_clock_hz = clock_get_hz(clk_sys);
    }

    return mia_normal_sys_clock_hz;
}

// Calculates the slowest PHI2 possible for a given system clock.
// The PIO divider has a fixed maximum, so very slow PHI2 may need clk_sys slowed too.
static uint32_t mia_min_phi2_frequency_for_sys_clock(uint32_t system_freq) {
    uint32_t max_divider_cycles = mia_max_divider_cycles();
    return (system_freq + max_divider_cycles - 1u) / max_divider_cycles;
}

// Calculates the slowest PHI2 possible with the currently running clk_sys.
static uint32_t mia_min_phi2_frequency(void) {
    return mia_min_phi2_frequency_for_sys_clock(clock_get_hz(clk_sys));
}

// Calculates the fastest PHI2 possible with the currently running clk_sys.
// A PIO divider of 1 means one PHI2 period every 32 PIO cycles.
static uint32_t mia_max_phi2_frequency(void) {
    return clock_get_hz(clk_sys) / MIA_PHI2_PIO_CYCLES_PER_PERIOD;
}

// Clamps the requested user-facing speed to MIA's supported public range.
static uint32_t mia_clamp_requested_phi2_frequency(uint32_t target_hz) {
    if (target_hz < MIA_MIN_PHI2_HZ) {
        return MIA_MIN_PHI2_HZ;
    }

    if (target_hz > MIA_MAX_PHI2_HZ) {
        return MIA_MAX_PHI2_HZ;
    }

    return target_hz;
}

// Attempts to set clk_sys using the system PLL.
// This is used for normal clocks, slow PLL clocks, and the overclock path.
static bool mia_set_sys_clock_from_pll(uint32_t target_hz) {
    return set_sys_clock_hz(target_hz, false);
}

// Attempts to derive clk_sys directly from the crystal oscillator.
// This is useful for very slow debug clocks that the PLL cannot generate exactly.
static bool mia_set_sys_clock_from_xosc(uint32_t target_hz) {
    return clock_configure(clk_sys,
                           CLOCKS_CLK_SYS_CTRL_SRC_VALUE_CLKSRC_CLK_SYS_AUX,
                           CLOCKS_CLK_SYS_CTRL_AUXSRC_VALUE_XOSC_CLKSRC,
                           XOSC_HZ,
                           target_hz);
}

// Sets the CYW43 PIO clock divisor used by the Wi-Fi chip link.
// This must track clk_sys when MIA slows down or overclocks the Pico.
static void mia_set_cyw43_clock_divisor(uint32_t div_int) {
    cyw43_set_pio_clock_divisor(div_int, MIA_CYW43_CLKDIV_FRAC8);
}

// Changes clk_sys and adjusts the CYW43 divisor in the safe order.
// When increasing clk_sys, slow the CYW43 PIO link first. When decreasing
// clk_sys, change the system clock first and then speed the CYW43 link back up.
static bool mia_set_sys_clock_with_cyw43_divisor(uint32_t target_hz,
                                                 bool use_xosc,
                                                 uint32_t cyw43_div_int) {
    uint32_t current_hz = clock_get_hz(clk_sys);

    if (target_hz > current_hz) {
        mia_set_cyw43_clock_divisor(cyw43_div_int);
    }

    bool ok = use_xosc ? mia_set_sys_clock_from_xosc(target_hz)
                       : mia_set_sys_clock_from_pll(target_hz);

    if (!ok) {
        return false;
    }

    if (target_hz <= current_hz) {
        mia_set_cyw43_clock_divisor(cyw43_div_int);
    }

    return true;
}

// Restores the boot-time system clock.
// Ordinary PHI2 requests use the normal clock and only adjust the PIO divider.
static bool mia_restore_normal_sys_clock(void) {
    return mia_set_sys_clock_with_cyw43_divisor(mia_normal_sys_clock_frequency(),
                                               false,
                                               MIA_CYW43_NORMAL_CLKDIV_INT);
}

// Chooses the system clock needed for the requested PHI2 speed.
// Low speeds slow the whole Pico, high speeds overclock it, and normal speeds
// keep the boot clock untouched.
static void mia_reclock_for_phi2(uint32_t target_hz) {
    target_hz = mia_clamp_requested_phi2_frequency(target_hz);

    uint32_t normal_sys_clock_hz = mia_normal_sys_clock_frequency();
    uint32_t normal_min_phi2_hz = mia_min_phi2_frequency_for_sys_clock(normal_sys_clock_hz);
    uint32_t normal_max_phi2_hz = normal_sys_clock_hz / MIA_PHI2_PIO_CYCLES_PER_PERIOD;

    if (target_hz < normal_min_phi2_hz) {
        // Slow PHI2 needs a slower clk_sys because the PIO divider is already maxed out.
        uint32_t slow_sys_clock_hz =
            (uint32_t)((uint64_t)target_hz * mia_max_divider_cycles());

        if (slow_sys_clock_hz <= XOSC_HZ) {
            // Very slow requests can be made from the crystal clock without PLL limits.
            if (!mia_set_sys_clock_with_cyw43_divisor(slow_sys_clock_hz,
                                                      true,
                                                      MIA_CYW43_SLOW_CLKDIV_INT)) {
                (void)mia_set_sys_clock_with_cyw43_divisor(MIA_SLOW_SYS_CLOCK_HZ,
                                                           false,
                                                           MIA_CYW43_SLOW_CLKDIV_INT);
            }
            return;
        }

        (void)mia_set_sys_clock_with_cyw43_divisor(MIA_SLOW_SYS_CLOCK_HZ,
                                                   false,
                                                   MIA_CYW43_SLOW_CLKDIV_INT);
        return;
    }

    if (target_hz > normal_max_phi2_hz) {
        // Fast PHI2 needs a faster clk_sys because the PIO divider cannot go below 1.
        if (!mia_set_sys_clock_with_cyw43_divisor(MIA_FAST_SYS_CLOCK_HZ,
                                                  false,
                                                  MIA_CYW43_FAST_CLKDIV_INT)) {
            (void)mia_restore_normal_sys_clock();
        }
        return;
    }

    (void)mia_restore_normal_sys_clock();
}

// Clamps the requested speed to what the currently selected clk_sys and PIO divider can do.
// This runs after reclocking so the limits are based on the final system clock.
static uint32_t mia_clamp_phi2_frequency(uint32_t target_hz) {
    target_hz = mia_clamp_requested_phi2_frequency(target_hz);
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

// Calculates the PIO divider needed to generate the target PHI2 from clk_sys.
static float mia_phi2_divider(uint32_t target_hz) {
    float pio_freq = target_hz * (float)MIA_PHI2_PIO_CYCLES_PER_PERIOD;
    float system_freq = (float)clock_get_hz(clk_sys);
    float div = system_freq / pio_freq;

    if (div < 1.0f) div = 1.0f;
    if (div > 65536.0f) div = 65536.0f;

    return div;
}

// Configures a PIO state machine with the divider for the requested PHI2.
// This is used before the state machine is started.
void mia_speed_configure_phi2(pio_sm_config *config, uint32_t target_hz) {
    mia_reclock_for_phi2(target_hz);
    target_hz = mia_clamp_phi2_frequency(target_hz);
    sm_config_set_clkdiv(config, mia_phi2_divider(target_hz));
}

// Applies a PHI2 speed to every state machine that is synchronized to PHI2.
static void mia_speed_apply_phi2(uint32_t target_hz) {
    mia_reclock_for_phi2(target_hz);
    target_hz = mia_clamp_phi2_frequency(target_hz);
    float div = mia_phi2_divider(target_hz);

    pio_sm_set_clkdiv(MIA_WRITE_PIO, MIA_WRITE_SM, div);
    pio_sm_set_clkdiv(MIA_READ_PIO, MIA_READ_SM, div);
    pio_sm_set_clkdiv(MIA_ACT_PIO, MIA_ACT_SM, div);

    mia_applied_phi2_hz = target_hz;
}

// Re-applies the current speed after all PIO programs have been initialized.
void mia_speed_apply_current(void) {
    mia_speed_apply_phi2(mia_applied_phi2_hz);
}

// Resets speed request bookkeeping without changing the applied PHI2 clock.
// A runtime reset should restart the 6502, not unexpectedly change its speed.
void mia_speed_reset_runtime_state(void) {
    mia_staged_phi2_hz = mia_applied_phi2_hz;
    mia_requested_phi2_hz = mia_applied_phi2_hz;
    mia_speed_change_requested = false;
    mia_status_clear_flag(MIA_STAT_SPEED_CHANGING);
}

// Applies a committed speed change from the main MIA service loop.
// The 6502-visible status and IRQ are updated after the new speed is active.
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
