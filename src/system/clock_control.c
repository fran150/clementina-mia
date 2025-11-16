/**
 * Clock Control Implementation
 * PWM-based clock generation using Raspberry Pi PWM module
 */

#include "clock_control.h"
#include "hardware/gpio_mapping.h"
#include "hardware/pwm.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "pico/time.h"
#include <stdio.h>
#include <math.h>

static clock_phase_t current_phase = CLOCK_PHASE_BOOT;
static uint slice_num;
static uint channel;

static void clock_control_set_frequency(uint32_t frequency_hz) {    
    // Get system clock frequency
    uint32_t sys_clk = clock_get_hz(clk_sys);
    
    // Calculate optimal PWM parameters for precise frequency generation
    // PWM frequency = sys_clk / (divider * (wrap + 1))
    // We want to maximize wrap for better resolution while staying within limits
    
    float best_divider = 1.0f;
    uint32_t best_wrap = 0;
    float best_error = 1.0f;
    
    // Try different divider values to find the best frequency match
    for (uint32_t div_int = 1; div_int <= 255; div_int++) {
        for (uint32_t div_frac = 0; div_frac < 16; div_frac++) {
            float divider = div_int + (div_frac / 16.0f);
            uint32_t wrap = (uint32_t)((float)sys_clk / (divider * frequency_hz)) - 1;
            
            if (wrap > 0 && wrap <= 65535) {
                float actual_freq = (float)sys_clk / (divider * (wrap + 1));
                float error = fabsf(actual_freq - frequency_hz) / frequency_hz;
                
                if (error < best_error) {
                    best_error = error;
                    best_divider = divider;
                    best_wrap = wrap;
                }
            }
        }
    }
    
    // Configure PWM with optimal parameters
    pwm_config config = pwm_get_default_config();
    pwm_config_set_clkdiv(&config, best_divider);
    pwm_config_set_wrap(&config, best_wrap);
    
    // Set 50% duty cycle
    uint32_t level = best_wrap / 2;
    
    // Apply configuration
    pwm_init(slice_num, &config, false);
    pwm_set_chan_level(slice_num, channel, level);
    pwm_set_enabled(slice_num, true);
        
    printf("Clock frequency set to %lu Hz (divider: %.2f, wrap: %lu, error: %.4f%%)\n", 
           frequency_hz, best_divider, best_wrap, best_error * 100.0f);
}

void clock_control_init(void) {
    // Set up GPIO 28 for PWM output (PWM6A)
    gpio_set_function(GPIO_CLK_OUT, GPIO_FUNC_PWM);
    
    // Get PWM slice number and channel for GPIO 28
    slice_num = pwm_gpio_to_slice_num(GPIO_CLK_OUT);
    channel = pwm_gpio_to_channel(GPIO_CLK_OUT);
        
    // Start with boot phase frequency
    clock_control_set_frequency(CLOCK_FREQ_BOOT);
    
    printf("Clock control initialized on GPIO %d (PWM slice %d, channel %d)\n", 
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