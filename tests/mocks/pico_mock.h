/**
 * Pico SDK Mock for Host-Based Testing
 * 
 * Provides minimal mocks of Pico SDK functions needed for unit tests
 * to run natively on the host machine without hardware.
 */

#ifndef PICO_MOCK_H
#define PICO_MOCK_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "irq/irq.h"


// Define uint type (used by Pico SDK)
typedef unsigned int uint;

// Mock PIO types and structures
typedef struct pio_hw pio_hw_t;
typedef pio_hw_t *PIO;

// Mock PIO instances
#define pio0 ((PIO)0)
#define pio1 ((PIO)1)

// Mock printf (already works natively, but define for consistency)
#define printf printf

// Mock tight_loop_contents (no-op on host)
static inline void tight_loop_contents(void) {
    // No-op for host testing
}

// Mock sleep functions (no-op on host, or use actual sleep if needed)
static inline void sleep_ms(uint32_t ms) {
    (void)ms;
    // No-op for unit tests
}

static inline void sleep_us(uint64_t us) {
    (void)us;
    // No-op for unit tests
}

// Mock watchdog functions
// Forward declare indexed_memory_init so we can call it
void indexed_memory_init(void);

static inline void watchdog_reboot(uint32_t pc, uint32_t sp, uint32_t delay_ms) {
    (void)pc;
    (void)sp;
    (void)delay_ms;
    printf("Mock watchdog reboot triggered - simulating system reboot\n");
    // In tests, simulate a reboot by reinitializing the indexed memory system
    // This mimics what would happen after a real reboot
    irq_init();
    indexed_memory_init();
}

#endif // PICO_MOCK_H
