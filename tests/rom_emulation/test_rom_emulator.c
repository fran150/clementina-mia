/**
 * ROM Emulator Test Functions
 * Basic validation tests for ROM emulation system
 */

#include "test_rom_emulator.h"
#include <stdio.h>

// Only compile hardware-dependent test functions when building for Pico
#ifdef PICO_BUILD
#include "rom_emulation/rom_emulator.h"
#include "hardware/gpio_mapping.h"
#include "system/clock_control.h"

void rom_emulator_test_basic_functionality(void) {
    printf("=== ROM Emulator Basic Functionality Test ===\n");
    
    // Test initialization
    printf("Testing initialization...\n");
    rom_emulator_init();
    
    // Verify initial state
    rom_state_t initial_state = rom_emulator_get_state();
    printf("  Initial state: %d (expected: 0) - %s\n", 
           initial_state, (initial_state == ROM_STATE_INACTIVE) ? "PASS" : "FAIL");
    
    // Test kernel size reporting
    uint32_t kernel_size = rom_emulator_get_kernel_size();
    printf("  Kernel size: %lu bytes - %s\n", 
           kernel_size, (kernel_size > 0) ? "PASS" : "FAIL");
    
    // Test bytes transferred (should be 0 initially)
    uint32_t bytes_transferred = rom_emulator_get_bytes_transferred();
    printf("  Initial bytes transferred: %lu (expected: 0) - %s\n", 
           bytes_transferred, (bytes_transferred == 0) ? "PASS" : "FAIL");
    
    // Test active status
    bool is_active = rom_emulator_is_active();
    printf("  Initial active status: %s (expected: false) - %s\n", 
           is_active ? "true" : "false", (!is_active) ? "PASS" : "FAIL");
    
    printf("=== ROM Emulator Basic Test Complete ===\n\n");
}

void rom_emulator_test_boot_sequence(void) {
    printf("=== ROM Emulator Boot Sequence Test ===\n");
    
    // Initialize systems
    gpio_mapping_init();
    clock_control_init();
    rom_emulator_init();
    
    // Start boot sequence
    printf("Starting boot sequence...\n");
    rom_emulator_start_boot_sequence();
    
    // Check state transition
    rom_state_t state = rom_emulator_get_state();
    printf("  State after start: %d (expected: 1 for RESET_SEQUENCE) - %s\n", 
           state, (state == ROM_STATE_RESET_SEQUENCE) ? "PASS" : "FAIL");
    
    // Wait for reset sequence to complete
    printf("Waiting for reset sequence...\n");
    sleep_ms(1);  // Wait 1ms for reset to complete
    
    // Process the ROM emulator to advance state
    rom_emulator_process();
    
    state = rom_emulator_get_state();
    printf("  State after reset: %d (expected: 2 for BOOT_ACTIVE) - %s\n", 
           state, (state == ROM_STATE_BOOT_ACTIVE) ? "PASS" : "FAIL");
    
    printf("=== Boot Sequence Test Complete ===\n\n");
}

void rom_emulator_test_memory_reads(void) {
    printf("=== ROM Emulator Memory Read Test ===\n");
    
    // Initialize and start boot sequence
    gpio_mapping_init();
    clock_control_init();
    rom_emulator_init();
    rom_emulator_start_boot_sequence();
    sleep_ms(1);
    rom_emulator_process();
    
    uint8_t data;
    bool result;
    
    // Test reset vector reads ($FFFC-$FFFD maps to $3FC-$3FD in MIA space)
    printf("Testing reset vector reads...\n");
    
    result = rom_emulator_handle_read(0x03FC, &data);  // Reset vector low byte
    printf("  Reset vector low byte: 0x%02X (expected: 0x00) - %s\n", 
           data, (result && data == 0x00) ? "PASS" : "FAIL");
    
    result = rom_emulator_handle_read(0x03FD, &data);  // Reset vector high byte
    printf("  Reset vector high byte: 0x%02X (expected: 0xE0) - %s\n", 
           data, (result && data == 0xE0) ? "PASS" : "FAIL");
    
    // Test boot loader code reads
    printf("Testing boot loader code reads...\n");
    
    result = rom_emulator_handle_read(0x0000, &data);  // First instruction (SEI)
    printf("  First instruction: 0x%02X (expected: 0x78 for SEI) - %s\n", 
           data, (result && data == 0x78) ? "PASS" : "FAIL");
    
    result = rom_emulator_handle_read(0x0001, &data);  // Second instruction (CLD)
    printf("  Second instruction: 0x%02X (expected: 0xD8 for CLD) - %s\n", 
           data, (result && data == 0xD8) ? "PASS" : "FAIL");
    
    // Test kernel status register
    printf("Testing kernel status register...\n");
    
    result = rom_emulator_handle_read(0x0100, &data);  // Status register
    printf("  Status register: 0x%02X (expected: 0x01 for data available) - %s\n", 
           data, (result && data == 0x01) ? "PASS" : "FAIL");
    
    // Test kernel data register (read a few bytes)
    printf("Testing kernel data register...\n");
    
    result = rom_emulator_handle_read(0x0101, &data);  // First kernel byte
    printf("  First kernel byte: 0x%02X (expected: 0x78 for SEI) - %s\n", 
           data, (result && data == 0x78) ? "PASS" : "FAIL");
    
    uint32_t bytes_transferred = rom_emulator_get_bytes_transferred();
    printf("  Bytes transferred after first read: %lu (expected: 1) - %s\n", 
           bytes_transferred, (bytes_transferred == 1) ? "PASS" : "FAIL");
    
    result = rom_emulator_handle_read(0x0101, &data);  // Second kernel byte
    printf("  Second kernel byte: 0x%02X (expected: 0xD8 for CLD) - %s\n", 
           data, (result && data == 0xD8) ? "PASS" : "FAIL");
    
    bytes_transferred = rom_emulator_get_bytes_transferred();
    printf("  Bytes transferred after second read: %lu (expected: 2) - %s\n", 
           bytes_transferred, (bytes_transferred == 2) ? "PASS" : "FAIL");
    
    printf("=== Memory Read Test Complete ===\n\n");
}

void rom_emulator_test_complete_transfer(void) {
    printf("=== ROM Emulator Complete Transfer Test ===\n");
    
    // Initialize and start boot sequence
    gpio_mapping_init();
    clock_control_init();
    rom_emulator_init();
    rom_emulator_start_boot_sequence();
    sleep_ms(1);
    rom_emulator_process();
    
    uint32_t kernel_size = rom_emulator_get_kernel_size();
    printf("Total kernel size: %lu bytes\n", kernel_size);
    
    // Simulate reading all kernel data
    uint8_t data;
    uint32_t bytes_read = 0;
    
    printf("Simulating complete kernel transfer...\n");
    
    // Read all kernel bytes
    for (uint32_t i = 0; i < kernel_size; i++) {
        bool result = rom_emulator_handle_read(0x0101, &data);  // Data register
        if (result) {
            bytes_read++;
        } else {
            printf("  ERROR: Failed to read byte %lu\n", i);
            break;
        }
        
        // Check progress every 64 bytes
        if ((i + 1) % 64 == 0 || i == kernel_size - 1) {
            uint32_t transferred = rom_emulator_get_bytes_transferred();
            printf("  Progress: %lu/%lu bytes transferred\n", transferred, kernel_size);
        }
    }
    
    // Check final state
    rom_state_t final_state = rom_emulator_get_state();
    printf("  Final state: %d (expected: 3 for COMPLETE) - %s\n", 
           final_state, (final_state == ROM_STATE_COMPLETE) ? "PASS" : "FAIL");
    
    // Check status register after completion
    bool result = rom_emulator_handle_read(0x0100, &data);  // Status register
    printf("  Final status register: 0x%02X (expected: 0x00 for complete) - %s\n", 
           data, (result && data == 0x00) ? "PASS" : "FAIL");
    
    printf("  Total bytes read: %lu (expected: %lu) - %s\n", 
           bytes_read, kernel_size, (bytes_read == kernel_size) ? "PASS" : "FAIL");
    
    printf("=== Complete Transfer Test Complete ===\n\n");
}

#endif // PICO_BUILD

/**
 * Run all ROM emulator tests
 */
bool run_rom_emulator_tests(void) {
    printf("\n=== Running ROM Emulator Tests ===\n");
    
    // Note: ROM emulator tests require hardware initialization
    // For now, we'll just report that tests are skipped in host mode
    printf("ROM emulator tests require hardware - skipping in host mode\n");
    printf("Run firmware with RUN_ROM_EMULATOR_TESTS defined for hardware tests\n");
    
    printf("=== ROM Emulator Tests SKIPPED ===\n\n");
    return true;  // Return true to not fail the test suite
}
