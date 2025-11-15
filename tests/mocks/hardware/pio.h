/**
 * Mock hardware/pio.h for host-based testing
 * 
 * Provides minimal PIO definitions needed for unit tests.
 */

#ifndef HARDWARE_PIO_MOCK_H
#define HARDWARE_PIO_MOCK_H

#include "../pico_mock.h"

// PIO interrupt sources
typedef enum {
    pis_interrupt0 = 0,
    pis_interrupt1 = 1,
    pis_interrupt2 = 2,
    pis_interrupt3 = 3,
} pio_interrupt_source_t;

// Mock PIO functions (no-op for testing)
static inline void pio_interrupt_clear(PIO pio, uint irq) {
    (void)pio;
    (void)irq;
}

static inline bool pio_sm_is_rx_fifo_empty(PIO pio, uint sm) {
    (void)pio;
    (void)sm;
    return true;  // Mock: always empty
}

static inline bool pio_sm_is_tx_fifo_full(PIO pio, uint sm) {
    (void)pio;
    (void)sm;
    return false;  // Mock: never full
}

static inline bool pio_sm_is_tx_fifo_empty(PIO pio, uint sm) {
    (void)pio;
    (void)sm;
    return true;  // Mock: always empty
}

static inline bool pio_sm_is_rx_fifo_full(PIO pio, uint sm) {
    (void)pio;
    (void)sm;
    return false;  // Mock: never full
}

static inline uint8_t pio_sm_get(PIO pio, uint sm) {
    (void)pio;
    (void)sm;
    return 0;  // Mock: return dummy data
}

static inline void pio_sm_put(PIO pio, uint sm, uint32_t data) {
    (void)pio;
    (void)sm;
    (void)data;
}

static inline uint8_t pio_sm_get_rx_fifo_level(PIO pio, uint sm) {
    (void)pio;
    (void)sm;
    return 0;  // Mock: FIFO empty
}

static inline uint8_t pio_sm_get_tx_fifo_level(PIO pio, uint sm) {
    (void)pio;
    (void)sm;
    return 0;  // Mock: FIFO empty
}

static inline void pio_set_irq0_source_enabled(PIO pio, pio_interrupt_source_t source, bool enabled) {
    (void)pio;
    (void)source;
    (void)enabled;
}

#endif // HARDWARE_PIO_MOCK_H
