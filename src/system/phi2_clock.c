#include "hardware/pio.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "phi2_clock.pio.h"

#include "phi2_clock.h"

static PIO pio = pio1;
static uint sm = 1;
static uint phi2_pin = GPIO_CLK_OUT;   // example GPIO

void phi2_clock_init(uint32_t freq_hz) {
    uint offset = pio_add_program(pio, &phi2_clock_program);

    pio_sm_config c = phi2_clock_program_get_default_config(offset);

    sm_config_set_sideset_pins(&c, phi2_pin);
    sm_config_set_out_pins(&c, phi2_pin, 1);
    sm_config_set_set_pins(&c, phi2_pin, 1);

    pio_gpio_init(pio, phi2_pin);
    pio_sm_set_consecutive_pindirs(pio, sm, phi2_pin, 1, true);

    uint32_t sys_clk = clock_get_hz(clk_sys);
    uint32_t half_period = (sys_clk / (2 * freq_hz)) - 1;

    sm_config_set_clkdiv(&c, 1.0f);

    pio_sm_init(pio, sm, offset, &c);
    pio_sm_put_blocking(pio, sm, half_period);
    pio_sm_exec(pio, sm, pio_encode_pull(false, false));

    pio_sm_set_enabled(pio, sm, true);
}
