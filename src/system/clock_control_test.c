/**
 * Clock Control Test Functions
 * Basic validation tests for PWM clock generation system
 */

#include "clock_control.h"
#include <stdio.h>
#include "pico/time.h"

void clock_control_test_basic_functionality(void) {
    printf("=== Clock Control Basic Functionality Test ===\n");
    
    // Test initialization
    printf("Testing initialization...\n");
    clock_control_init();
    
    // Test frequency validation
    printf("Testing frequency validation...\n");
    bool valid_100k = clock_control_validate_frequency(CLOCK_FREQ_BOOT);
    bool valid_1m = clock_control_validate_frequency(CLOCK_FREQ_NORMAL);
    bool invalid_low = clock_control_validate_frequency(100);  // Too low
    bool invalid_high = clock_control_validate_frequency(50000000);  // Too high
    
    printf("  100kHz validation: %s\n", valid_100k ? "PASS" : "FAIL");
    printf("  1MHz validation: %s\n", valid_1m ? "PASS" : "FAIL");
    printf("  Invalid low freq: %s\n", !invalid_low ? "PASS" : "FAIL");
    printf("  Invalid high freq: %s\n", !invalid_high ? "PASS" : "FAIL");
    
    // Test phase switching
    printf("Testing phase switching...\n");
    clock_control_set_phase(CLOCK_PHASE_BOOT);
    uint32_t boot_freq = clock_control_get_frequency();
    
    clock_control_set_phase(CLOCK_PHASE_NORMAL);
    uint32_t normal_freq = clock_control_get_frequency();
    
    printf("  Boot phase freq: %lu Hz (expected: %d Hz) - %s\n", 
           boot_freq, CLOCK_FREQ_BOOT, 
           (boot_freq == CLOCK_FREQ_BOOT) ? "PASS" : "FAIL");
    printf("  Normal phase freq: %lu Hz (expected: %d Hz) - %s\n", 
           normal_freq, CLOCK_FREQ_NORMAL, 
           (normal_freq == CLOCK_FREQ_NORMAL) ? "PASS" : "FAIL");
    
    // Test enable/disable
    printf("Testing enable/disable...\n");
    clock_control_enable(true);
    bool enabled = clock_control_is_enabled();
    clock_control_enable(false);
    bool disabled = !clock_control_is_enabled();
    clock_control_enable(true);  // Re-enable for normal operation
    
    printf("  Enable test: %s\n", enabled ? "PASS" : "FAIL");
    printf("  Disable test: %s\n", disabled ? "PASS" : "FAIL");
    
    // Test stability monitoring (wait for stability)
    printf("Testing stability monitoring...\n");
    clock_control_set_frequency(CLOCK_FREQ_NORMAL);
    sleep_ms(15);  // Wait for stability validation period
    bool stable = clock_control_is_stable();
    float deviation = clock_control_get_deviation();
    
    printf("  Stability: %s\n", stable ? "PASS" : "FAIL");
    printf("  Deviation: %.4f%% (max: %.1f%%) - %s\n", 
           deviation * 100.0f, CLOCK_DEVIATION_MAX * 100.0f,
           (deviation <= CLOCK_DEVIATION_MAX) ? "PASS" : "FAIL");
    
    printf("=== Clock Control Test Complete ===\n\n");
}

void clock_control_test_frequency_accuracy(void) {
    printf("=== Clock Control Frequency Accuracy Test ===\n");
    
    uint32_t test_frequencies[] = {
        CLOCK_FREQ_BOOT,    // 100 kHz
        CLOCK_FREQ_NORMAL,  // 1 MHz
        500000,             // 500 kHz
        2000000,            // 2 MHz
        250000              // 250 kHz
    };
    
    int num_tests = sizeof(test_frequencies) / sizeof(test_frequencies[0]);
    
    for (int i = 0; i < num_tests; i++) {
        uint32_t target_freq = test_frequencies[i];
        printf("Testing frequency: %lu Hz\n", target_freq);
        
        if (clock_control_validate_frequency(target_freq)) {
            clock_control_set_frequency(target_freq);
            sleep_ms(15);  // Wait for stability
            
            float deviation = clock_control_get_deviation();
            bool stable = clock_control_is_stable();
            
            printf("  Deviation: %.4f%% - %s\n", 
                   deviation * 100.0f,
                   (deviation <= CLOCK_DEVIATION_MAX) ? "PASS" : "FAIL");
            printf("  Stability: %s\n", stable ? "PASS" : "FAIL");
        } else {
            printf("  Frequency validation failed - SKIP\n");
        }
    }
    
    printf("=== Frequency Accuracy Test Complete ===\n\n");
}