/**
 * ROM Emulator Test Functions
 * Test function declarations for ROM emulation system
 */

#ifndef TEST_ROM_EMULATOR_H
#define TEST_ROM_EMULATOR_H

#include <stdbool.h>

// Test function prototypes
void rom_emulator_test_basic_functionality(void);
void rom_emulator_test_boot_sequence(void);
void rom_emulator_test_memory_reads(void);
void rom_emulator_test_complete_transfer(void);

// Main test runner
bool run_rom_emulator_tests(void);

#endif // TEST_ROM_EMULATOR_H