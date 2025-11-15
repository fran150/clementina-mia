/**
 * Mock implementation of PIO-C FIFO communication for host-based testing
 * 
 * Provides mock implementations of PIO functions that would normally
 * require hardware, allowing tests to run on the host machine.
 */

#include "bus_interface/bus_sync_pio.h"
#include <stdio.h>
#include <stdbool.h>

// Mock FIFO state
static uint8_t mock_rx_fifo[8];
static uint8_t mock_tx_fifo[8];
static uint8_t mock_rx_level = 0;
static uint8_t mock_tx_level = 0;
static bool mock_initialized = false;

/**
 * Initialize the synchronous bus interface PIO (mock)
 */
void bus_sync_pio_init(void) {
    printf("Mock: bus_sync_pio_init() called\n");
    
    // Reset FIFO state
    mock_rx_level = 0;
    mock_tx_level = 0;
    mock_initialized = true;
}

/**
 * PIO IRQ handler (mock)
 * 
 * In real hardware, this would be called when CS is sampled active at 200ns.
 * For testing, we just verify it can be called without crashing.
 */
void bus_sync_pio_irq_handler(void) {
    // Mock implementation - just verify it doesn't crash
    // In real hardware, this would handle the timing-critical bus protocol
}

/**
 * Process WRITE data from RX FIFO (mock)
 */
bool bus_sync_pio_process_write_data(void) {
    if (!mock_initialized) {
        return false;
    }
    
    // Simulate processing data from RX FIFO
    if (mock_rx_level > 0) {
        mock_rx_level--;
        return true;
    }
    
    return false;
}

/**
 * Check if PIO is ready for next cycle (mock)
 */
bool bus_sync_pio_is_ready(void) {
    // Initialize if not already done (for tests that don't call init)
    if (!mock_initialized) {
        bus_sync_pio_init();
    }
    return true;  // Always ready in mock
}

/**
 * Get PIO statistics for debugging (mock)
 */
void bus_sync_pio_get_stats(uint8_t *rx_level, uint8_t *tx_level, bool *stalled) {
    if (rx_level) {
        *rx_level = mock_rx_level;
    }
    
    if (tx_level) {
        *tx_level = mock_tx_level;
    }
    
    if (stalled) {
        // Never stalled in mock
        *stalled = false;
    }
}

/**
 * Check for FIFO overflow/underflow conditions (mock)
 */
void bus_sync_pio_check_fifo_errors(bool *rx_overflow, bool *tx_underflow) {
    // Initialize if not already done (for tests that don't call init)
    if (!mock_initialized) {
        bus_sync_pio_init();
    }
    
    if (rx_overflow) {
        // Simulate overflow if RX FIFO is full
        *rx_overflow = (mock_rx_level >= 8);
    }
    
    if (tx_underflow) {
        // In mock, TX FIFO is never underflowed (no real PIO pulling data)
        // This is normal for mock - PIO would be blocking on pull in real hardware
        *tx_underflow = false;
    }
}

// Mock helper functions for testing

/**
 * Simulate pushing data to RX FIFO (for testing)
 */
void mock_pio_push_rx(uint8_t data) {
    if (mock_rx_level < 8) {
        mock_rx_fifo[mock_rx_level] = data;
        mock_rx_level++;
    }
}

/**
 * Simulate pulling data from TX FIFO (for testing)
 */
uint8_t mock_pio_pull_tx(void) {
    if (mock_tx_level > 0) {
        mock_tx_level--;
        return mock_tx_fifo[mock_tx_level];
    }
    return 0;
}

/**
 * Reset mock FIFO state (for testing)
 */
void mock_pio_reset(void) {
    mock_rx_level = 0;
    mock_tx_level = 0;
}
