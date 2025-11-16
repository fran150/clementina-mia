/**
 * Reset Control Implementation
 * Hardware reset line management
 */

#include "reset_control.h"
#include "hardware/gpio_mapping.h"
#include "hardware/gpio.h"
#include "pico/time.h"

static bool reset_asserted = false;
static absolute_time_t reset_start_time;

void reset_control_assert_reset(void) {
    gpio_put(GPIO_RESET_OUT, 0);  // Assert reset (active low)
    reset_asserted = true;
    reset_start_time = get_absolute_time();
}

void reset_control_release_reset(void) {
    gpio_put(GPIO_RESET_OUT, 1);  // Release reset (active low)
    reset_asserted = false;
}

bool reset_control_is_reset_asserted(void) {
    return reset_asserted;
}

void reset_control_process(void) {
    // Auto-release reset after minimum assertion time
    if (reset_asserted) {
        if (absolute_time_diff_us(reset_start_time, get_absolute_time()) >= (RESET_ASSERT_TIME_MS * 1000)) {
            reset_control_release_reset();
        }
    }
}