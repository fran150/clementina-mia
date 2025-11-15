/**
 * MIA Synchronous Bus Interface PIO Integration Implementation
 * 
 * Implements the C side of the hybrid PIO + C synchronous bus protocol.
 */

#include "bus_sync_pio.h"
#include "bus_interface.h"
#include "indexed_memory/indexed_memory.h"
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

// Track last address for WRITE operations
// When a WRITE occurs, we store the address here so we can process
// the data when it arrives in the RX FIFO later
static volatile uint8_t last_write_addr = 0;
static volatile bool write_pending = false;

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
 * 
 * OPTIMIZATION NOTES:
 * - Marked as __attribute__((optimize("O3"))) for maximum speed
 * - Uses inline assembly for precise timing delays
 * - Minimizes function calls in critical path
 * - Speculatively prepares READ data to maximize available time
 */
void __attribute__((optimize("O3"))) bus_sync_pio_irq_handler(void) {
    // Clear the IRQ flag
    pio_interrupt_clear(pio_instance, BUS_PIO_IRQ);
    
    // =========================================================================
    // PHASE 1: Read address and speculatively prepare READ data (200-400ns)
    // =========================================================================
    
    // Check if RX FIFO has data available (should always be true)
    // PIO pushes address at 60ns, we're called at 200ns
    if (pio_sm_is_rx_fifo_empty(pio_instance, sm)) {
        // FIFO underflow - this should never happen
        // PIO should have pushed address before triggering IRQ
        // This indicates a critical timing failure
        
        // Set error status and trigger interrupt
        indexed_memory_set_status(STATUS_MEMORY_ERROR);
        indexed_memory_trigger_irq(IRQ_MEMORY_ERROR);
        
        // Push NOP control byte and return
        pio_sm_put(pio_instance, sm, BUS_CTRL_NOP);
        return;
    }
    
    // Read address from RX FIFO (pushed by PIO at 60ns)
    // This is non-blocking because PIO already pushed the address
    uint8_t addr = pio_sm_get(pio_instance, sm);
    
    // Speculatively prepare READ data (assume READ operation)
    // This takes ~150-200ns but we have 330ns available (200-530ns)
    // If this turns out to be a WRITE, we'll discard this data
    // OPTIMIZATION: bus_interface_read() is optimized for fast execution
    uint8_t data = bus_interface_read(addr);
    
    // =========================================================================
    // PHASE 2: Wait for PHI2 to rise (400-500ns)
    // =========================================================================
    
    // Busy-wait for PHI2 to rise (500ns mark)
    // This is a tight loop that polls GPIO 28 (PHI2 clock)
    // At 133 MHz, this loop runs for ~100ns = ~13 cycles
    // OPTIMIZATION: Direct GPIO register access for minimum latency
    // Using gpio_get() is already optimized by the SDK, but we could
    // use direct register access if needed: (sio_hw->gpio_in & (1u << BUS_PHI2_PIN))
    while (!gpio_get(BUS_PHI2_PIN)) {
        // Busy wait for PHI2 = HIGH
        // This is acceptable because:
        // 1. We're in an IRQ handler (must be fast anyway)
        // 2. Only ~100ns of waiting (~13 CPU cycles)
        // 3. No other useful work can be done during this time
        // 4. Speculative data preparation is already complete
    }
    
    // PHI2 is now HIGH (at 500ns mark)
    
    // =========================================================================
    // PHASE 3: Wait 30ns for OE/WE to settle, then read them (530ns)
    // =========================================================================
    
    // Wait 30ns for OE and WE to settle after PHI2 rises
    // At 133 MHz: 30ns = ~4 CPU cycles
    // Use inline assembly NOPs for precise timing
    // OPTIMIZATION: Inline assembly ensures exact cycle count
    __asm volatile(
        "nop\n"
        "nop\n"
        "nop\n"
        "nop\n"
        ::: "memory"
    );
    
    // Now at ~530ns - OE and WE are valid!
    // Read OE pin (GPIO 19) - active low
    // Read WE pin (GPIO 18) - active low
    // OPTIMIZATION: Read both pins in quick succession to minimize time
    bool oe_active = !gpio_get(BUS_OE_PIN);
    bool we_active = !gpio_get(BUS_WE_PIN);
    
    // =========================================================================
    // PHASE 4: Determine operation type and push response (540-560ns)
    // =========================================================================
    
    // Determine operation type based on OE and WE
    // OPTIMIZATION: Use if-else chain for fastest path (READ is most common)
    if (!oe_active) {
        // OE is inactive (HIGH) - MIA should not drive the bus
        // This could happen if:
        // - CS was active but OE is not (unusual but possible)
        // - Timing glitch or invalid bus cycle
        // OPTIMIZATION: Inline loop unrolling for GPIO direction setting
        gpio_set_dir(8, GPIO_IN);
        gpio_set_dir(9, GPIO_IN);
        gpio_set_dir(10, GPIO_IN);
        gpio_set_dir(11, GPIO_IN);
        gpio_set_dir(12, GPIO_IN);
        gpio_set_dir(13, GPIO_IN);
        gpio_set_dir(14, GPIO_IN);
        gpio_set_dir(15, GPIO_IN);
        
        // Check if TX FIFO has space before pushing
        if (pio_sm_is_tx_fifo_full(pio_instance, sm)) {
            // TX FIFO overflow - this should never happen
            // PIO should be blocking on pull, waiting for our response
            // This indicates a serious timing problem
            
            // Set error status and trigger interrupt
            indexed_memory_set_status(STATUS_MEMORY_ERROR);
            indexed_memory_trigger_irq(IRQ_MEMORY_ERROR);
            
            return;  // Cannot push, abort
        }
        
        // Push NOP control byte
        pio_sm_put(pio_instance, sm, BUS_CTRL_NOP);
        
    } else if (!we_active) {
        // OE is active (LOW) and WE is inactive (HIGH)
        // This is a READ operation: R/W = HIGH (read)
        // MOST COMMON PATH - optimized for speed
        
        // Configure data bus as outputs before PIO drives it
        // OPTIMIZATION: Inline loop unrolling for GPIO direction setting
        gpio_set_dir(8, GPIO_OUT);
        gpio_set_dir(9, GPIO_OUT);
        gpio_set_dir(10, GPIO_OUT);
        gpio_set_dir(11, GPIO_OUT);
        gpio_set_dir(12, GPIO_OUT);
        gpio_set_dir(13, GPIO_OUT);
        gpio_set_dir(14, GPIO_OUT);
        gpio_set_dir(15, GPIO_OUT);
        
        // Check if TX FIFO has space for control byte + data byte
        if (pio_sm_is_tx_fifo_full(pio_instance, sm)) {
            // TX FIFO overflow - cannot push response
            // This indicates a critical timing failure
            
            // Set error status and trigger interrupt
            indexed_memory_set_status(STATUS_MEMORY_ERROR);
            indexed_memory_trigger_irq(IRQ_MEMORY_ERROR);
            
            // Tri-state bus and abort
            gpio_set_dir(8, GPIO_IN);
            gpio_set_dir(9, GPIO_IN);
            gpio_set_dir(10, GPIO_IN);
            gpio_set_dir(11, GPIO_IN);
            gpio_set_dir(12, GPIO_IN);
            gpio_set_dir(13, GPIO_IN);
            gpio_set_dir(14, GPIO_IN);
            gpio_set_dir(15, GPIO_IN);
            
            return;
        }
        
        // Use our speculatively prepared data!
        // OPTIMIZATION: Push both control and data in quick succession
        pio_sm_put(pio_instance, sm, BUS_CTRL_READ);
        
        // Check again for data byte (FIFO should have space for 2 entries)
        if (pio_sm_is_tx_fifo_full(pio_instance, sm)) {
            // TX FIFO overflow after control byte
            // This is a critical error - PIO is expecting data
            
            // Set error status and trigger interrupt
            indexed_memory_set_status(STATUS_MEMORY_ERROR);
            indexed_memory_trigger_irq(IRQ_MEMORY_ERROR);
            
            // Tri-state bus and abort
            gpio_set_dir(8, GPIO_IN);
            gpio_set_dir(9, GPIO_IN);
            gpio_set_dir(10, GPIO_IN);
            gpio_set_dir(11, GPIO_IN);
            gpio_set_dir(12, GPIO_IN);
            gpio_set_dir(13, GPIO_IN);
            gpio_set_dir(14, GPIO_IN);
            gpio_set_dir(15, GPIO_IN);
            
            return;
        }
        
        pio_sm_put(pio_instance, sm, data);
        
    } else {
        // OE is active (LOW) and WE is active (LOW)
        // This is a WRITE operation: R/W = LOW (write)
        
        // Ensure data bus is configured as inputs
        // OPTIMIZATION: Inline loop unrolling for GPIO direction setting
        gpio_set_dir(8, GPIO_IN);
        gpio_set_dir(9, GPIO_IN);
        gpio_set_dir(10, GPIO_IN);
        gpio_set_dir(11, GPIO_IN);
        gpio_set_dir(12, GPIO_IN);
        gpio_set_dir(13, GPIO_IN);
        gpio_set_dir(14, GPIO_IN);
        gpio_set_dir(15, GPIO_IN);
        
        // Check if TX FIFO has space before pushing
        if (pio_sm_is_tx_fifo_full(pio_instance, sm)) {
            // TX FIFO overflow - cannot push response
            
            // Set error status and trigger interrupt
            indexed_memory_set_status(STATUS_MEMORY_ERROR);
            indexed_memory_trigger_irq(IRQ_MEMORY_ERROR);
            
            return;
        }
        
        // Store address for later processing when data arrives
        last_write_addr = addr;
        write_pending = true;
        
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
 * Process WRITE data from RX FIFO
 * 
 * After a WRITE operation, the PIO pushes the latched data byte to RX FIFO.
 * This function should be called periodically to process pending WRITE data.
 * 
 * OPTIMIZATION: This function is called from the main loop or a lower-priority
 * interrupt to process WRITE data without blocking the critical IRQ handler.
 * 
 * @return true if data was processed, false if FIFO was empty
 */
bool bus_sync_pio_process_write_data(void) {
    // Check if we have a pending write operation
    if (!write_pending) {
        return false;  // No pending write
    }
    
    // Check if RX FIFO has data available
    if (pio_sm_is_rx_fifo_empty(pio_instance, sm)) {
        return false;  // Data not yet available
    }
    
    // Read data from RX FIFO (latched by PIO at PHI2 falling edge)
    uint8_t data = pio_sm_get(pio_instance, sm);
    
    // Process the write using the stored address
    // OPTIMIZATION: bus_interface_write() is called outside the critical IRQ path
    bus_interface_write(last_write_addr, data);
    
    // Clear pending flag
    write_pending = false;
    
    return true;
}

/**
 * Check if PIO is ready for next cycle
 */
bool bus_sync_pio_is_ready(void) {
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

/**
 * Check for FIFO overflow/underflow conditions
 * 
 * @param rx_overflow Pointer to store RX FIFO overflow status
 * @param tx_underflow Pointer to store TX FIFO underflow status
 */
void bus_sync_pio_check_fifo_errors(bool *rx_overflow, bool *tx_underflow) {
    if (rx_overflow) {
        // RX FIFO overflow occurs when PIO tries to push but FIFO is full
        // This would indicate C code is not consuming data fast enough
        *rx_overflow = pio_sm_is_rx_fifo_full(pio_instance, sm);
    }
    
    if (tx_underflow) {
        // TX FIFO underflow occurs when PIO tries to pull but FIFO is empty
        // This would indicate C code is not providing data fast enough
        // Note: PIO uses blocking pull, so this shouldn't happen in normal operation
        *tx_underflow = pio_sm_is_tx_fifo_empty(pio_instance, sm);
    }
}
