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
static uint32_t current_frequency = CLOCK_FREQ_BOOT;
static uint32_t target_frequency = CLOCK_FREQ_BOOT;
static uint slice_num;
static uint channel;
static absolute_time_t last_stability_check;
static bool stability_validated = false;

void clock_control_init(void) {
    // Set up GPIO 28 for PWM output (PWM6A)
    gpio_set_function(GPIO_CLK_OUT, GPIO_FUNC_PWM);
    
    // Get PWM slice number and channel for GPIO 28
    slice_num = pwm_gpio_to_slice_num(GPIO_CLK_OUT);
    channel = pwm_gpio_to_channel(GPIO_CLK_OUT);
    
    // Initialize stability monitoring
    last_stability_check = get_absolute_time();
    stability_validated = false;
    
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

void clock_control_set_frequency(uint32_t frequency_hz) {
    target_frequency = frequency_hz;
    
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
    
    // Update current frequency and reset stability validation
    current_frequency = frequency_hz;
    stability_validated = false;
    last_stability_check = get_absolute_time();
    
    printf("Clock frequency set to %lu Hz (divider: %.2f, wrap: %lu, error: %.4f%%)\n", 
           frequency_hz, best_divider, best_wrap, best_error * 100.0f);
}

uint32_t clock_control_get_frequency(void) {
    return current_frequency;
}

clock_phase_t clock_control_get_phase(void) {
    return current_phase;
}

bool clock_control_is_stable(void) {
    // Check if enough time has passed for stability validation
    absolute_time_t now = get_absolute_time();
    int64_t elapsed_us = absolute_time_diff_us(last_stability_check, now);
    
    // Require at least 10ms for stability validation
    if (elapsed_us < 10000) {
        return false;
    }
    
    // For PWM-based clock generation, stability is primarily determined by
    // the accuracy of our frequency calculation and system clock stability
    float deviation = clock_control_get_deviation();
    stability_validated = (deviation <= CLOCK_DEVIATION_MAX);
    
    return stability_validated;
}

float clock_control_get_deviation(void) {
    // Calculate theoretical deviation based on PWM parameters
    uint32_t sys_clk = clock_get_hz(clk_sys);
    
    // Note: In a real implementation, we'd read back the actual PWM config
    // For now, we'll calculate based on our target frequency
    
    // Calculate actual frequency that would be generated
    float divider = (float)sys_clk / (target_frequency * 65536.0f);
    if (divider < 1.0f) divider = 1.0f;
    
    uint32_t wrap = (uint32_t)((float)sys_clk / (divider * target_frequency)) - 1;
    float actual_freq = (float)sys_clk / (divider * (wrap + 1));
    
    // Calculate deviation as percentage
    float deviation = fabsf(actual_freq - target_frequency) / target_frequency;
    
    return deviation;
}

// Additional control functions for dynamic frequency adjustment
bool clock_control_validate_frequency(uint32_t frequency_hz) {
    // Validate that the requested frequency is within supported range
    if (frequency_hz < 1000 || frequency_hz > 10000000) {  // 1kHz to 10MHz range
        return false;
    }
    
    // Check if we can achieve acceptable accuracy for this frequency
    uint32_t sys_clk = clock_get_hz(clk_sys);
    float min_error = 1.0f;
    
    // Test if we can achieve the required accuracy
    for (uint32_t div_int = 1; div_int <= 255; div_int++) {
        for (uint32_t div_frac = 0; div_frac < 16; div_frac++) {
            float divider = div_int + (div_frac / 16.0f);
            uint32_t wrap = (uint32_t)((float)sys_clk / (divider * frequency_hz)) - 1;
            
            if (wrap > 0 && wrap <= 65535) {
                float actual_freq = (float)sys_clk / (divider * (wrap + 1));
                float error = fabsf(actual_freq - frequency_hz) / frequency_hz;
                
                if (error < min_error) {
                    min_error = error;
                }
            }
        }
    }
    
    return (min_error <= CLOCK_DEVIATION_MAX);
}

void clock_control_enable(bool enable) {
    pwm_set_enabled(slice_num, enable);
    if (enable) {
        printf("Clock output enabled\n");
    } else {
        printf("Clock output disabled\n");
    }
}

bool clock_control_is_enabled(void) {
    // Check if PWM slice is enabled
    return (pwm_hw->slice[slice_num].csr & PWM_CH0_CSR_EN_BITS) != 0;
}

void clock_control_reset(void) {
    // Disable PWM
    pwm_set_enabled(slice_num, false);
    
    // Reset to boot phase
    current_phase = CLOCK_PHASE_BOOT;
    stability_validated = false;
    
    // Reinitialize with boot frequency
    clock_control_set_frequency(CLOCK_FREQ_BOOT);
    
    printf("Clock control reset to boot phase\n");
}