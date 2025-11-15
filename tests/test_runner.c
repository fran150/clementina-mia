/**
 * MIA Test Runner
 * 
 * Main entry point for running all unit tests on the host machine.
 * This allows fast iteration without needing to flash hardware.
 */

#include <stdio.h>
#include <stdbool.h>

// Include test headers
#include "bus_interface/test_bus_interface.h"
#include "bus_interface/test_bus_sync_pio_fifo.h"
#include "indexed_memory/test_indexed_memory.h"
#include "rom_emulation/test_rom_emulator.h"
#include "system/test_clock_control.h"

int main(void) {
    printf("===========================================\n");
    printf("MIA Unit Test Suite\n");
    printf("===========================================\n\n");
    
    bool all_passed = true;
    int total_suites = 0;
    int passed_suites = 0;
    
    // Run bus interface tests
    printf("Running Bus Interface Tests...\n");
    total_suites++;
    if (run_bus_interface_tests()) {
        passed_suites++;
        printf("✓ Bus Interface Tests PASSED\n\n");
    } else {
        all_passed = false;
        printf("✗ Bus Interface Tests FAILED\n\n");
    }
    
    // Run indexed memory tests
    printf("Running Indexed Memory Tests...\n");
    total_suites++;
    if (run_indexed_memory_tests()) {
        passed_suites++;
        printf("✓ Indexed Memory Tests PASSED\n\n");
    } else {
        all_passed = false;
        printf("✗ Indexed Memory Tests FAILED\n\n");
    }
    
    // Run ROM emulator tests
    printf("Running ROM Emulator Tests...\n");
    total_suites++;
    if (run_rom_emulator_tests()) {
        passed_suites++;
        printf("✓ ROM Emulator Tests PASSED\n\n");
    } else {
        all_passed = false;
        printf("✗ ROM Emulator Tests FAILED\n\n");
    }
    
    // Run clock control tests
    printf("Running Clock Control Tests...\n");
    total_suites++;
    if (run_clock_control_tests()) {
        passed_suites++;
        printf("✓ Clock Control Tests PASSED\n\n");
    } else {
        all_passed = false;
        printf("✗ Clock Control Tests FAILED\n\n");
    }
    
    // Run PIO-C FIFO communication tests
    printf("Running PIO-C FIFO Communication Tests...\n");
    total_suites++;
    test_bus_sync_pio_fifo_run_all();
    int fifo_run = 0, fifo_passed = 0, fifo_failed = 0;
    test_bus_sync_pio_fifo_get_results(&fifo_run, &fifo_passed, &fifo_failed);
    if (fifo_failed == 0 && fifo_run > 0) {
        passed_suites++;
        printf("✓ PIO-C FIFO Communication Tests PASSED\n\n");
    } else {
        all_passed = false;
        printf("✗ PIO-C FIFO Communication Tests FAILED\n\n");
    }
    
    // Print summary
    printf("===========================================\n");
    printf("Test Summary: %d/%d suites passed\n", passed_suites, total_suites);
    printf("===========================================\n");
    
    if (all_passed) {
        printf("✓ ALL TESTS PASSED\n");
        return 0;
    } else {
        printf("✗ SOME TESTS FAILED\n");
        return 1;
    }
}
