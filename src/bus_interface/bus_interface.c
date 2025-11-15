/**
 * MIA Bus Interface Module Implementation
 * 
 * Implements the 6502 bus interface for the indexed memory system.
 * Provides register access handlers for multi-window architecture with shared registers.
 * 
 * NOTE: MIA only sees 8-bit addresses (A0-A7 on GPIO 0-7)
 * The IO0_CS chip select line indicates we're in indexed interface mode
 */

#include "bus_interface.h"
#include "indexed_memory/indexed_memory.h"
#include <stddef.h>
#include <string.h>

// ============================================================================
// Window State Management
// ============================================================================

// Global window state array supporting up to 8 windows (A-H)
// Exposed for direct access - window_num is always valid (0-7) from address decoding
window_state_t g_window_state[MAX_WINDOWS];

// ============================================================================
// Register Handler Functions
// ============================================================================

/**
 * Read IDX_SELECT register
 * Returns the currently selected index for the specified window
 * 
 * OPTIMIZATION: Inline for fastest access
 * 
 * @param window_num Window number (0-7 for Windows A-H)
 * @return Currently selected index (0-255)
 */
static inline uint8_t read_idx_select(uint8_t window_num) {
    // Return the active index for the specified window
    return g_window_state[window_num].active_index;
}

/**
 * Write IDX_SELECT register
 * Updates the active index selection for the specified window
 * 
 * OPTIMIZATION: Inline for fastest access
 * 
 * @param window_num Window number (0-7 for Windows A-H)
 * @param index Index to select (0-255)
 */
static inline void write_idx_select(uint8_t window_num, uint8_t index) {
    // Update the active index for the specified window
    g_window_state[window_num].active_index = index;
}

/**
 * Read DATA_PORT register
 * Reads a byte from the currently selected index with auto-stepping
 * 
 * OPTIMIZATION: Inline for fastest access (most common operation)
 * 
 * @param window_num Window number (0-7 for Windows A-H)
 * @return Data byte read from the current index address
 */
static inline uint8_t read_data_port(uint8_t window_num) {
    // Get the currently selected index for this window
    uint8_t idx = g_window_state[window_num].active_index;
    
    // Read byte from index with auto-stepping
    // indexed_memory_read() handles auto-stepping based on index configuration
    return indexed_memory_read(idx);
}

/**
 * Write DATA_PORT register
 * Writes a byte to the currently selected index with auto-stepping
 * 
 * OPTIMIZATION: Inline for fastest access (common operation)
 * 
 * @param window_num Window number (0-7 for Windows A-H)
 * @param data Data byte to write to the current index address
 */
static inline void write_data_port(uint8_t window_num, uint8_t data) {
    // Get the currently selected index for this window
    uint8_t idx = g_window_state[window_num].active_index;
    
    // Write byte to index with auto-stepping
    // indexed_memory_write() handles auto-stepping based on index configuration
    indexed_memory_write(idx, data);
}

/**
 * Read CFG_FIELD_SELECT register
 * Returns the currently selected configuration field for the specified window
 * 
 * OPTIMIZATION: Inline for fastest access
 * 
 * @param window_num Window number (0-7 for Windows A-H)
 * @return Currently selected configuration field
 */
static inline uint8_t read_cfg_field_select(uint8_t window_num) {
    // Return the selected configuration field for the specified window
    return g_window_state[window_num].config_field_select;
}

/**
 * Write CFG_FIELD_SELECT register
 * Updates the configuration field selection for the specified window
 * 
 * OPTIMIZATION: Inline for fastest access
 * 
 * @param window_num Window number (0-7 for Windows A-H)
 * @param field Configuration field to select
 */
static inline void write_cfg_field_select(uint8_t window_num, uint8_t field) {
    // Update the configuration field selection for the specified window
    g_window_state[window_num].config_field_select = field;
}

/**
 * Read CFG_DATA register
 * Reads the selected configuration field from the active index
 * 
 * @param window_num Window number (0-7 for Windows A-H)
 * @return Configuration field value
 */
static uint8_t read_cfg_data(uint8_t window_num) {
    // Get the currently selected index for this window
    uint8_t idx = g_window_state[window_num].active_index;
    
    // Get the selected configuration field for this window
    uint8_t field = g_window_state[window_num].config_field_select;
    
    // Read the configuration field from the index
    return indexed_memory_get_config_field(idx, field);
}

/**
 * Write CFG_DATA register
 * Writes to the selected configuration field of the active index
 * 
 * @param window_num Window number (0-7 for Windows A-H)
 * @param data Data to write to the configuration field
 */
static void write_cfg_data(uint8_t window_num, uint8_t data) {
    // Get the currently selected index for this window
    uint8_t idx = g_window_state[window_num].active_index;
    
    // Get the selected configuration field for this window
    uint8_t field = g_window_state[window_num].config_field_select;
    
    // Write the configuration field to the index
    indexed_memory_set_config_field(idx, field, data);
}

// ============================================================================
// Shared Register Handler Functions
// ============================================================================

/**
 * Read DEVICE_STATUS register
 * Returns the global device status register value
 * 
 * @return System status register value
 */
static uint8_t read_device_status(void) {
    // Return system status from indexed memory
    return indexed_memory_get_status();
}

/**
 * Read IRQ_CAUSE_LOW register
 * Returns the low byte of the interrupt cause register (bits 0-7)
 * 
 * @return Interrupt cause low byte
 */
static uint8_t read_irq_cause_low(void) {
    // Return low byte of IRQ cause from indexed memory
    return indexed_memory_get_irq_cause_low();
}

/**
 * Write IRQ_CAUSE_LOW register
 * Implements write-1-to-clear logic for interrupt acknowledgment
 * Writing 1 to a bit position clears that specific interrupt
 * 
 * @param clear_bits Bits to clear (1 = clear interrupt, 0 = no change)
 */
static void write_irq_cause_low(uint8_t clear_bits) {
    // Clear specified interrupt bits using write-1-to-clear logic
    indexed_memory_write_irq_cause_low(clear_bits);
}

/**
 * Read IRQ_CAUSE_HIGH register
 * Returns the high byte of the interrupt cause register (bits 8-15)
 * 
 * @return Interrupt cause high byte
 */
static uint8_t read_irq_cause_high(void) {
    // Return high byte of IRQ cause from indexed memory
    return indexed_memory_get_irq_cause_high();
}

/**
 * Write IRQ_CAUSE_HIGH register
 * Implements write-1-to-clear logic for interrupt acknowledgment
 * Writing 1 to a bit position clears that specific interrupt
 * 
 * @param clear_bits Bits to clear (1 = clear interrupt, 0 = no change)
 */
static void write_irq_cause_high(uint8_t clear_bits) {
    // Clear specified interrupt bits using write-1-to-clear logic
    indexed_memory_write_irq_cause_high(clear_bits);
}

/**
 * Read IRQ_MASK_LOW register
 * Returns the low byte of the interrupt mask register (bits 0-7)
 * 
 * @return Interrupt mask low byte
 */
static uint8_t read_irq_mask_low(void) {
    // Get full 16-bit mask and return low byte
    uint16_t mask = indexed_memory_get_irq_mask();
    return mask & 0xFF;
}

/**
 * Write IRQ_MASK_LOW register
 * Sets which interrupt sources are enabled (bits 0-7)
 * 
 * @param mask Interrupt mask low byte (1 = enabled, 0 = disabled)
 */
static void write_irq_mask_low(uint8_t mask) {
    // Get current mask, update low byte, and write back
    uint16_t current_mask = indexed_memory_get_irq_mask();
    uint16_t new_mask = (current_mask & 0xFF00) | mask;
    indexed_memory_set_irq_mask(new_mask);
}

/**
 * Read IRQ_MASK_HIGH register
 * Returns the high byte of the interrupt mask register (bits 8-15)
 * 
 * @return Interrupt mask high byte
 */
static uint8_t read_irq_mask_high(void) {
    // Get full 16-bit mask and return high byte
    uint16_t mask = indexed_memory_get_irq_mask();
    return (mask >> 8) & 0xFF;
}

/**
 * Write IRQ_MASK_HIGH register
 * Sets which interrupt sources are enabled (bits 8-15)
 * 
 * @param mask Interrupt mask high byte (1 = enabled, 0 = disabled)
 */
static void write_irq_mask_high(uint8_t mask) {
    // Get current mask, update high byte, and write back
    uint16_t current_mask = indexed_memory_get_irq_mask();
    uint16_t new_mask = (current_mask & 0x00FF) | ((uint16_t)mask << 8);
    indexed_memory_set_irq_mask(new_mask);
}

/**
 * Read IRQ_ENABLE register
 * Returns the global interrupt enable/disable state
 * 
 * @return Global interrupt enable (1 = enabled, 0 = disabled)
 */
static uint8_t read_irq_enable(void) {
    // Return global interrupt enable state
    return indexed_memory_get_irq_enable();
}

/**
 * Write IRQ_ENABLE register
 * Sets the global interrupt enable/disable state
 * 
 * @param enable Global interrupt enable (1 = enabled, 0 = disabled)
 */
static void write_irq_enable(uint8_t enable) {
    // Set global interrupt enable state
    indexed_memory_set_irq_enable(enable);
}

/**
 * Write COMMAND register (window-level)
 * Executes a window-level command on the currently selected index
 * 
 * @param window_num Window number (0-7 for Windows A-H)
 * @param command Command code to execute
 */
static void write_window_command(uint8_t window_num, uint8_t command) {
    // Get the currently selected index for this window
    uint8_t idx = g_window_state[window_num].active_index;
    
    // Execute window-level command on the selected index
    // Window commands: CMD_NOP, CMD_RESET_INDEX, CMD_SET_DEFAULT_TO_ADDR, CMD_SET_LIMIT_TO_ADDR
    indexed_memory_execute_window_command(idx, command);
}

/**
 * Write SHARED_COMMAND register (system-level)
 * Executes a system-wide command that affects the entire indexed memory system
 * 
 * @param command Command code to execute
 */
static void write_shared_command(uint8_t command) {
    // Execute shared/system-level command
    // Shared commands: CMD_SHARED_NOP, CMD_RESET_ALL, CMD_CLEAR_IRQ, CMD_COPY_BLOCK, CMD_SYSTEM_RESET
    indexed_memory_execute_shared_command(command);
}

// ============================================================================
// Module Initialization
// ============================================================================

void bus_interface_init(void) {
    // Initialize all window states to default values
    memset(g_window_state, 0, sizeof(g_window_state));
    
    // Set default active index for all windows to 0
    for (int i = 0; i < MAX_WINDOWS; i++) {
        g_window_state[i].active_index = 0;
        g_window_state[i].config_field_select = 0;
    }
    
    // Note: indexed_memory_init() should be called before this function
    // It is initialized in main.c before bus_interface_init()
    
    // TODO: Initialize GPIO pins for bus interface
    // TODO: Initialize PIO state machines for timing-critical operations
    // TODO: Set up interrupt handlers for bus operations
}

// ============================================================================
// Main Bus Interface Handlers
// ============================================================================

/**
 * Handle a READ operation from the 6502 bus
 * 
 * OPTIMIZATION NOTES:
 * - Marked as __attribute__((optimize("O3"))) for maximum speed
 * - Marked as __attribute__((hot)) to indicate this is a hot path
 * - Uses inline functions for register handlers to minimize call overhead
 * - Optimized for speculative execution (~150-200ns target)
 * - Most common path (DATA_PORT) is optimized first
 */
uint8_t __attribute__((optimize("O3"))) __attribute__((hot)) bus_interface_read(uint8_t local_addr) {
    // Decode 8-bit local address
    // OPTIMIZATION: Use bitwise operations for fastest decoding
    bool is_shared = (local_addr & 0x80) != 0;
    uint8_t window_num = (local_addr >> 4) & 0x07;
    uint8_t reg_offset = local_addr & 0x0F;  // Works for both shared and window space
        
    // Check if accessing shared register space
    if (is_shared) {
        // Handle shared register reads
        // OPTIMIZATION: Use if-else chain for most common registers first
        if (local_addr == REG_DEVICE_STATUS) {
            return read_device_status();
        } else if (local_addr == REG_IRQ_CAUSE_LOW) {
            return read_irq_cause_low();
        } else if (local_addr == REG_IRQ_CAUSE_HIGH) {
            return read_irq_cause_high();
        } else if (local_addr == REG_IRQ_MASK_LOW) {
            return read_irq_mask_low();
        } else if (local_addr == REG_IRQ_MASK_HIGH) {
            return read_irq_mask_high();
        } else if (local_addr == REG_IRQ_ENABLE) {
            return read_irq_enable();
        } else {
            // Reserved shared register or write-only register
            return 0x00;
        }
    }
    
    // Handle window register reads
    // OPTIMIZATION: Use if-else chain with most common operations first
    // DATA_PORT is the most frequently accessed register
    if (reg_offset == REG_OFFSET_DATA_PORT) {
        return read_data_port(window_num);
    } else if (reg_offset == REG_OFFSET_IDX_SELECT) {
        return read_idx_select(window_num);
    } else if (reg_offset == REG_OFFSET_CFG_DATA) {
        return read_cfg_data(window_num);
    } else if (reg_offset == REG_OFFSET_CFG_FIELD_SELECT) {
        return read_cfg_field_select(window_num);
    } else {
        // Reserved register offset (5-15) or write-only COMMAND register
        return 0x00;
    }
}

void bus_interface_write(uint8_t local_addr, uint8_t data) {
    // Decode 8-bit local address
    bool is_shared = (local_addr & 0x80) != 0;
    uint8_t window_num = (local_addr >> 4) & 0x07;
    uint8_t reg_offset = is_shared ? local_addr & 0x7F : local_addr & 0x0F;
    
    // Check if accessing shared register space
    if (is_shared) {
        // Handle shared register writes
        switch (local_addr) {
            case REG_DEVICE_STATUS:
                // Device status is read-only
                break;
                
            case REG_IRQ_CAUSE_LOW:
                write_irq_cause_low(data);
                break;
                
            case REG_IRQ_CAUSE_HIGH:
                write_irq_cause_high(data);
                break;
                
            case REG_IRQ_MASK_LOW:
                write_irq_mask_low(data);
                break;
                
            case REG_IRQ_MASK_HIGH:
                write_irq_mask_high(data);
                break;
                
            case REG_IRQ_ENABLE:
                write_irq_enable(data);
                break;
                
            case REG_SHARED_COMMAND:
                write_shared_command(data);
                break;
                
            default:
                // Reserved shared register - ignore writes
                break;
        }
        return;
    }
    
    // Handle window register writes
    // Route to appropriate register handler based on offset
    switch (reg_offset) {
        case REG_OFFSET_IDX_SELECT:
            write_idx_select(window_num, data);
            break;
            
        case REG_OFFSET_DATA_PORT:
            write_data_port(window_num, data);
            break;
            
        case REG_OFFSET_CFG_FIELD_SELECT:
            write_cfg_field_select(window_num, data);
            break;
            
        case REG_OFFSET_CFG_DATA:
            write_cfg_data(window_num, data);
            break;
            
        case REG_OFFSET_COMMAND:
            write_window_command(window_num, data);
            break;
            
        default:
            // Reserved register offset (5-15) - ignore writes
            break;
    }
}
