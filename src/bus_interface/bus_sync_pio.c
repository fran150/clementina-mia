/**
 * MIA Synchronous Bus Interface PIO Integration Implementation
 * 
 * Implements the C side of the hybrid PIO + C synchronous bus protocol.
 */

#include "bus_sync_pio.h"
#include "bus_interface.h"
#include "hardware/pio.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "pico/stdlib.h"

// Include the generated PIO header
#include "bus_sync.pio.h"

// PIO instance and state machine
static PIO pio_instance = BUS_PIO_INSTANCE;
static uint sm = BUS_PIO_SM;
static uint pio_offset = 0;

/**
 * Initialize the synchronous bus interface PIO
 */
void bus_sync_pio_init(void) {
    // Load PIO program into PIO memory
    pio_offset = pio_add_program(pio_instance, &bus_sync_program);
    
    // Initialize the PIO state machine
    bus_sync_program_init(pio_instance, sm, pio_offset);
    
    // Set up IRQ handler for PIO IRQ 0
    // The PIO will trigger this when CS is sampled active at 200ns
    irq_set_exclusive_handler(PIO0_IRQ_0, bus_sync_pio_irq_handler);
    irq_set_enabled(PIO0_IRQ_0, true);
    
    // Note: The PIO state machine is already started by bus_sync_program_init()
}

/**
 * PIO IRQ handler - called when CS is sampled active at 200ns
 * 
 * This implements the speculative execution strategy to handle the timing
 * constraint that OE and WE are only valid 30ns after PHI2 rises (at 530ns).
 */
void bus_sync_pio_irq_handler(void) {
    // Clear the IRQ flag
    pio_interrupt_clear(pio_instance, BUS_PIO_IRQ);
    
    // =========================================================================
    // PHASE 1: Read address and speculatively prepare READ data (200-400ns)
    // =========================================================================
    
    // Read address from RX FIFO (pushed by PIO at 60ns)
    // This is non-blocking because PIO already pushed the address
    uint8_t addr = pio_sm_get(pio_instance, sm);
    
    // Speculatively prepare READ data (assume READ operation)
    // This takes ~150-200ns but we have 330ns available (200-530ns)
    // If this turns out to be a WRITE, we'll discard this data
    uint8_t data = bus_interface_read(addr);
    
    // =========================================================================
    // PHASE 2: Wait for PHI2 to rise (400-500ns)
    // =========================================================================
    
    // Busy-wait for PHI2 to rise (500ns mark)
    // This is a tight loop that polls GPIO 28 (PHI2 clock)
    // At 133 MHz, this loop runs for ~100ns = ~13 cycles
    while (!gpio_get(BUS_PHI2_PIN)) {
        // Busy wait for PHI2 = HIGH
        // This is acceptable because:
        // 1. We're in an IRQ handler (must be fast anyway)
        // 2. Only ~100ns of waiting (~13 CPU cycles)
        // 3. No other useful work can be done during this time
    }
    
    // PHI2 is now HIGH (at 500ns mark)
    
    // =========================================================================
    // PHASE 3: Wait 30ns for OE/WE to settle, then read them (530ns)
    // =========================================================================
    
    // Wait 30ns for OE and WE to settle after PHI2 rises
    // At 133 MHz: 30ns = ~4 CPU cycles
    // Use inline assembly NOPs for precise timing
    __asm volatile("nop");
    __asm volatile("nop");
    __asm volatile("nop");
    __asm volatile("nop");
    
    // Now at ~530ns - OE and WE are valid!
    // Read OE pin (GPIO 19) - active low
    bool oe_active = !gpio_get(BUS_OE_PIN);
    
    // Read WE pin (GPIO 18) - active low
    // For 6502: R/W = !WE (WE low = write, WE high = read)
    bool we_active = !gpio_get(BUS_WE_PIN);
    
    // =========================================================================
    // PHASE 4: Determine operation type and push response (540-560ns)
    // =========================================================================
    
    // Determine operation type based on OE and WE
    if (!oe_active) {
        // OE is inactive (HIGH) - MIA should not drive the bus
        // This could happen if:
        // - CS was active but OE is not (unusual but possible)
        // - Timing glitch or invalid bus cycle
        // Push NOP control byte
        pio_sm_put(pio_instance, sm, BUS_CTRL_NOP);
        
    } else if (!we_active) {
        // OE is active (LOW) and WE is inactive (HIGH)
        // This is a READ operation: R/W = HIGH (read)
        // Use our speculatively prepared data!
        pio_sm_put(pio_instance, sm, BUS_CTRL_READ);
        pio_sm_put(pio_instance, sm, data);
        
    } else {
        // OE is active (LOW) and WE is active (LOW)
        // This is a WRITE operation: R/W = LOW (write)
        // Discard the speculatively prepared data
        // PIO will latch the write data at 1000ns and push to RX FIFO
        pio_sm_put(pio_instance, sm, BUS_CTRL_WRITE);
    }
    
    // IRQ handler complete
    // PIO will now unblock from the pull instruction and continue
    // For READ: PIO will pull data and drive bus by ~560ns
    // For WRITE: PIO will wait for PHI2 to fall and latch data at 1000ns
}

/**
 * Check if PIO is ready for next cycle
 */
bool bus_sync_pio_is_ready(void) {
    // Check if state machine is running
    if (!pio_sm_is_enabled(pio_instance, sm)) {
        return false;
    }
    
    // Check if FIFOs are not stalled
    // A stalled FIFO indicates a problem
    if (pio_sm_is_tx_fifo_full(pio_instance, sm) ||
        pio_sm_is_rx_fifo_empty(pio_instance, sm)) {
        // This is normal - TX FIFO full means C hasn't consumed data yet
        // RX FIFO empty means no pending operations
    }
    
    return true;
}

/**
 * Get PIO statistics for debugging
 */
void bus_sync_pio_get_stats(uint8_t *rx_level, uint8_t *tx_level, bool *stalled) {
    if (rx_level) {
        *rx_level = pio_sm_get_rx_fifo_level(pio_instance, sm);
    }
    
    if (tx_level) {
        *tx_level = pio_sm_get_tx_fifo_level(pio_instance, sm);
    }
    
    if (stalled) {
        // Check if state machine is stalled
        // This would indicate a timing problem
        *stalled = pio_sm_is_tx_fifo_full(pio_instance, sm) &&
                   pio_sm_is_rx_fifo_full(pio_instance, sm);
    }
}
