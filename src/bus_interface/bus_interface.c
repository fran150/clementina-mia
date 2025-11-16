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
#include "irq/irq.h"
#include <stddef.h>
#include <string.h>

// Access to indexed memory state for status field
extern indexed_memory_state_t g_state;

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
 * Read DATA_PORT register
 * Reads a byte from the currently selected index with auto-stepping
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
 * Read CFG_DATA register - optimized
 */
static inline uint8_t read_cfg_data(uint8_t window_num) {
    window_state_t *win = get_window_state(window_num);
    return indexed_memory_get_config_field(win->active_index, win->config_field_select);
}

/**
 * Write CFG_DATA register - optimized
 */
static inline void write_cfg_data(uint8_t window_num, uint8_t data) {
    window_state_t *win = get_window_state(window_num);
    indexed_memory_set_config_field(win->active_index, win->config_field_select, data);
}

// ============================================================================
// Shared Register Handler Functions - Now using macros for performance
// ============================================================================

/**
 * Read IRQ_MASK_LOW register
 * Returns the low byte of the interrupt mask register (bits 0-7)
 * 
 * @return Interrupt mask low byte
 */
static inline uint8_t read_irq_mask_low(void) {
    // Get full 16-bit mask and return low byte
    return irq_get_mask() & 0xFF;
}

/**
 * Write IRQ_MASK_LOW register
 * Sets which interrupt sources are enabled (bits 0-7)
 * 
 * @param mask Interrupt mask low byte (1 = enabled, 0 = disabled)
 */
static inline void write_irq_mask_low(uint8_t mask) {
    // Get current mask, update low byte, and write back
    uint16_t new_mask = (irq_get_mask() & 0xFF00) | mask;
    irq_set_mask(new_mask);
}

/**
 * Read IRQ_MASK_HIGH register
 * Returns the high byte of the interrupt mask register (bits 8-15)
 * 
 * @return Interrupt mask high byte
 */
static inline uint8_t read_irq_mask_high(void) {
    // Get full 16-bit mask and return high byte
    return (irq_get_mask() >> 8) & 0xFF;
}

/**
 * Write IRQ_MASK_HIGH register
 * Sets which interrupt sources are enabled (bits 8-15)
 * 
 * @param mask Interrupt mask high byte (1 = enabled, 0 = disabled)
 */
static inline void write_irq_mask_high(uint8_t mask) {
    // Get current mask, update high byte, and write back
    uint16_t new_mask = (irq_get_mask() & 0x00FF) | ((uint16_t)mask << 8);
    irq_set_mask(new_mask);
}

/**
 * Read IRQ_ENABLE register
 * Returns the global interrupt enable/disable state
 * 
 * @return Global interrupt enable (1 = enabled, 0 = disabled)
 */
static inline uint8_t read_irq_enable(void) {
    // Return global interrupt enable state
    return irq_get_enable();
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
 */
uint8_t __attribute__((optimize("O3"))) __attribute__((hot)) bus_interface_read(uint8_t local_addr) {
    // Decode 8-bit local address
    bool is_shared = (local_addr & 0x80) != 0;
    uint8_t window_num = (local_addr >> 4) & 0x07;
    uint8_t reg_offset = local_addr & 0x0F;  // Works for both shared and window space
        
    // Check if accessing shared register space
    if (is_shared) {
        // Handle shared register reads
        if (local_addr == REG_DEVICE_STATUS) {
            return g_state.status | (irq_is_pending() ? STATUS_IRQ_PENDING : 0);
        } else if (local_addr == REG_IRQ_CAUSE_LOW) {
            return irq_get_cause_low();
        } else if (local_addr == REG_IRQ_CAUSE_HIGH) {
            return irq_get_cause_high();
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
    // DATA_PORT is the most frequently accessed register
    if (reg_offset == REG_OFFSET_DATA_PORT) {
        return read_data_port(window_num);
    } else if (reg_offset == REG_OFFSET_IDX_SELECT) {
        return g_window_state[window_num].active_index;
    } else if (reg_offset == REG_OFFSET_CFG_DATA) {
        return read_cfg_data(window_num);
    } else if (reg_offset == REG_OFFSET_CFG_FIELD_SELECT) {
        return g_window_state[window_num].config_field_select;
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
                irq_write_cause_low(data);
                break;
                
            case REG_IRQ_CAUSE_HIGH:
                irq_write_cause_high(data);
                break;
                
            case REG_IRQ_MASK_LOW:
                write_irq_mask_low(data);
                break;
                
            case REG_IRQ_MASK_HIGH:
                write_irq_mask_high(data);
                break;
                
            case REG_IRQ_ENABLE:
                irq_set_enable(data);
                break;
                
            case REG_SHARED_COMMAND:
                indexed_memory_execute_shared_command(data);
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
            g_window_state[window_num].active_index = data;
            break;
            
        case REG_OFFSET_DATA_PORT:
            write_data_port(window_num, data);
            break;
            
        case REG_OFFSET_CFG_FIELD_SELECT:
            g_window_state[window_num].config_field_select = data;
            break;
            
        case REG_OFFSET_CFG_DATA:
            write_cfg_data(window_num, data);
            break;
            
        case REG_OFFSET_COMMAND:
            {
                uint8_t idx = g_window_state[window_num].active_index;
                indexed_memory_execute_window_command(idx, data);
            }
            break;
            
        default:
            // Reserved register offset (5-15) - ignore writes
            break;
    }
}
