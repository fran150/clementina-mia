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

#endif // PICO_MOCK_H
