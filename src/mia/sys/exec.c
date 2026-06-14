#include "exec.h"

#include <stdio.h>

#include "hardware/gpio.h"
#include "hardware/pio.h"

#include "etc/status.h"
#include "hardware/gpio_mapping.h"
#include "hardware/pio_mapping.h"

static volatile bool exec_paused;

void mia_exec_pause(void) {
    if (exec_paused) {
        return;
    }

    pio_sm_set_enabled(MIA_WRITE_PIO, MIA_WRITE_SM, false);

    // While paused, SIO owns PHI2 and holds it low. The write PIO state machine
    // state is left intact so resume can continue without rebuilding its Y base.
    gpio_set_function(CPU_PHI2_PIN, GPIO_FUNC_SIO);
    gpio_set_dir(CPU_PHI2_PIN, GPIO_OUT);
    gpio_put(CPU_PHI2_PIN, false);

    exec_paused = true;
    mia_status_set_flag(MIA_STAT_EXEC_PAUSED);
}

void mia_exec_resume(void) {
    if (!exec_paused) {
        return;
    }

    pio_gpio_init(MIA_WRITE_PIO, CPU_PHI2_PIN);
    pio_sm_set_enabled(MIA_WRITE_PIO, MIA_WRITE_SM, true);

    exec_paused = false;
    mia_status_clear_flag(MIA_STAT_EXEC_PAUSED);
}

bool mia_exec_is_paused(void) {
    return exec_paused;
}

void mia_exec_print_status(void) {
    printf("Exec: %s  PHI2:%s\n",
           exec_paused ? "paused" : "running",
           exec_paused ? "stopped-low" : "running");
}
