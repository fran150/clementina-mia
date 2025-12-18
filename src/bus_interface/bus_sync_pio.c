/**
 * MIA Synchronous Bus Interface PIO Integration Implementation
 * 
 * Implements the C side of the hybrid PIO + C synchronous bus protocol.
 */

#include "bus_sync_pio.h"
#include "bus_interface.h"
#include "indexed_memory/indexed_memory.h"
#include "irq/irq.h"
#include "hardware/pio.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "pico/stdlib.h"
#include "hardware/clocks.h"

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

// Forward declaration of IRQ1 handler
void bus_sync_pio_write_irq_handler(void);

/**
 * Initialize the synchronous bus interface PIO
 */
void bus_sync_pio_init(void) {
    // Load PIO program into PIO memory
    pio_offset = pio_add_program(pio_instance, &bus_sync_program);
    
    // Set up IRQ handler for PIO IRQ 0
    // The PIO will trigger this when CS is sampled active at 200ns
    irq_set_exclusive_handler(PIO0_IRQ_0, bus_sync_pio_irq_handler);
    irq_set_enabled(PIO0_IRQ_0, true);
    // Set up IRQ handler for PIO IRQ 1 (write-data notification)
    irq_set_exclusive_handler(PIO0_IRQ_1, bus_sync_pio_write_irq_handler);
    irq_set_enabled(PIO0_IRQ_1, true);

    // Initialize the PIO state machine
    bus_sync_program_init(pio_instance, sm, pio_offset);
        
    // Note: The PIO state machine is already started by bus_sync_program_init()

    // Enable PIO IRQ1 source as well (used for write-data notification)
    pio_set_irq1_source_enabled(pio_instance, pis_interrupt1, true);
}

/**
 * PIO IRQ1 handler - called when PIO signals write data is available (irq 1)
 * Reads the latched write data from the RX FIFO and processes it immediately.
 */
void __attribute__((optimize("O3"))) bus_sync_pio_write_irq_handler(void) {
    // Clear IRQ1 flag
    pio_interrupt_clear(pio_instance, 1);

    // Read data from RX FIFO if available
    if (!pio_sm_is_rx_fifo_empty(pio_instance, sm)) {
        uint8_t data = pio_sm_get(pio_instance, sm);

        // Consume pending write using stored address
        if (write_pending) {
            bus_interface_write(last_write_addr, data);
            write_pending = false;
        } else {
            // Spurious write data - set error
            indexed_memory_set_status(STATUS_MEMORY_ERROR);
            irq_set_bits(IRQ_MEMORY_ERROR);
        }
    }
}

/**
 * PIO IRQ handler - called when CS is sampled active at 200ns
 * 
 * This implements the speculative execution strategy to handle the timing
 * constraint that OE and WE are only valid 30ns after PHI2 rises (at 530ns).
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
        irq_set_bits(IRQ_MEMORY_ERROR);
        
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
    // PHASE 2: Wait for PHI2 to rise and OE/WE to settle (400-530ns)
    // =========================================================================
    
    // Wait for the OE/WE sample pushed by the PIO to arrive in RX FIFO.
    // The PIO captured OE/WE after PHI2 rose and pushed a 20-bit sample
    // (pins 0..19) into the RX FIFO. We read that sample and extract
    // the OE/WE pin states instead of sampling GPIOs directly here.
    while (pio_sm_is_rx_fifo_empty(pio_instance, sm)) {
        tight_loop_contents();
    }
    uint32_t sample = pio_sm_get(pio_instance, sm);

    /* Extract OE/WE bits from the sampled pins word. */
    bool oe_active = !((sample >> GPIO_OE) & 0x1);
    bool we_active = !((sample >> GPIO_WE) & 0x1);
    
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
        // PIO ensures data bus remains tri-stated (inputs)
        
        // Push NOP control byte (blocking put ensures PIO receives it)
        pio_sm_put_blocking(pio_instance, sm, BUS_CTRL_NOP);
        
    } else if (!we_active) {
        // OE is active (LOW) and WE is inactive (HIGH)
        // This is a READ operation: R/W = HIGH (read)
        // MOST COMMON PATH - optimized for speed
        
        // PIO will configure data bus as outputs before driving
        // Use our speculatively prepared data!
        // Blocking puts keep code simple and avoid unnecessary checks.
        pio_sm_put_blocking(pio_instance, sm, BUS_CTRL_READ);
        pio_sm_put_blocking(pio_instance, sm, data);
        
    } else {
        // OE is active (LOW) and WE is active (LOW)
        // This is a WRITE operation: R/W = LOW (write)
        
        // PIO ensures data bus remains as inputs for WRITE operations
        
        // Push WRITE control (blocking put)
        // If blocking put fails to make progress, it's an indication
        // of a critical timing failure elsewhere (PIO not waiting).
        
        // Check for race condition: if a write is already pending, we can't handle another
        if (write_pending) {
            // This means the main loop hasn't processed the previous write in time.
            // This is a "missed write" error.
            
            // Set error status and trigger interrupt
            indexed_memory_set_status(STATUS_MEMORY_ERROR);
            irq_set_bits(IRQ_MEMORY_ERROR);
            
            // Push NOP to PIO to ignore this write cycle and prevent FIFO desync
            pio_sm_put_blocking(pio_instance, sm, BUS_CTRL_NOP);
            return;
        }
        
        // Store address for later processing when data arrives
        last_write_addr = addr;
        write_pending = true;
        
        // Discard the speculatively prepared data
        // PIO will latch the write data at 1000ns and push to RX FIFO
        pio_sm_put_blocking(pio_instance, sm, BUS_CTRL_WRITE);
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
