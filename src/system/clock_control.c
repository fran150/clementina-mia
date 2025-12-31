/**
 * Clock Control Implementation
 * PWM-based clock generation using Raspberry Pi PWM module
 */

#include "clock_control.h"
#include "debug/debug_helper.h"
#include "hardware/gpio_mapping.h"
#include "hardware/pwm.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "pico/time.h"
#include <math.h>

static clock_phase_t current_phase = CLOCK_PHASE_BOOT;
static uint slice_num;
static uint channel;

#define MAX_PWM_BIT_COUNTER 65535 

int clock_control_set_frequency(uint32_t freq) {
    // System clock (usually 125 MHz)
    uint32_t sys_clk_hz = clock_get_hz(clk_sys);

    log_print(LOG_DEBUG, "Getting clock frequency: %li Hz\n", sys_clk_hz);

    // Try to find suitable wrap and clkdiv
    uint32_t top = MAX_PWM_BIT_COUNTER;  // Max 16-bit counter
    float clkdiv = (float)sys_clk_hz / (freq * (top + 1));

    log_print(LOG_DEBUG, "Clock divider: %f \n", clkdiv);

    // Ensure clkdiv is within valid range
    if (clkdiv < 1.0f) {
        clkdiv = 1.0f;
        top = sys_clk_hz / (freq * clkdiv) - 1;
    } else if (clkdiv > 255.0f) {
        return -1;
    }

    log_print(LOG_DEBUG, "Setting clock configuration GPIO %d (PWM slice %d, channel %d, divider %f, top %li)\n", 
        GPIO_CLK_OUT, slice_num, channel, clkdiv, top);

    // Set the PWM clock divdider, wrap counter and duty cycle
    pwm_set_clkdiv(slice_num, clkdiv);
    pwm_set_wrap(slice_num, top);
    pwm_set_chan_level(slice_num, channel, (top + 1) / 2);

    return 0;
}

void clock_control_init(void) {
    // Set the pin to PWM function
    gpio_set_function(GPIO_CLK_OUT, GPIO_FUNC_PWM);

    // Get PWM slice number and channel
    slice_num = pwm_gpio_to_slice_num(GPIO_CLK_OUT);
    channel = pwm_gpio_to_channel(GPIO_CLK_OUT);

    // Start with boot phase frequency
    clock_control_set_frequency(CLOCK_FREQ_BOOT);

    pwm_set_enabled(slice_num, true);
    
    log_print(LOG_INFO, "Clock control initialized on GPIO %d (PWM slice %d, channel %d)\n", 
           GPIO_CLK_OUT, slice_num, channel);
}

void clock_control_set_phase(clock_phase_t phase) {
    current_phase = phase;
    
    switch (phase) {
        case CLOCK_PHASE_BOOT:
            clock_control_set_frequency(CLOCK_FREQ_BOOT);
            break;
        case CLOCK_PHASE_NORMAL:
            clock_control_set_frequency(CLOCK_FREQ_NORMAL);
            break;
    }
}

void clock_control_reset(void) {
    // Disable PWM
    pwm_set_enabled(slice_num, false);
    
    // Reset to boot phase
    current_phase = CLOCK_PHASE_BOOT;
    
    // Reinitialize with boot frequency
    clock_control_set_frequency(CLOCK_FREQ_BOOT);
    
    printf("Clock control reset to boot phase\n");
}