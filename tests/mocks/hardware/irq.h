/**
 * Mock hardware/irq.h for host-based testing
 */

#ifndef HARDWARE_IRQ_MOCK_H
#define HARDWARE_IRQ_MOCK_H

#include "../pico_mock.h"

// IRQ numbers
#define PIO0_IRQ_0 7

// IRQ handler type
typedef void (*irq_handler_t)(void);

// Mock IRQ functions
static inline void irq_set_exclusive_handler(uint num, irq_handler_t handler) {
    (void)num;
    (void)handler;
}

static inline void irq_set_enabled(uint num, bool enabled) {
    (void)num;
    (void)enabled;
}

#endif // HARDWARE_IRQ_MOCK_H
