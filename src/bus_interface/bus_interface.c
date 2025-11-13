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
 * @param window_num Window number (0-7 for Windows A-H)
 * @return Currently selected index (0-255)
 */
static uint8_t read_idx_select(uint8_t window_num) {
    // Return the active index for the specified window
    return g_window_state[window_num].active_index;
}

/**
 * Write IDX_SELECT register
 * Updates the active index selection for the specified window
 * 
 * @param window_num Window number (0-7 for Windows A-H)
 * @param index Index to select (0-255)
 */
static void write_idx_select(uint8_t window_num, uint8_t index) {
    // Update the active index for the specified window
    g_window_state[window_num].active_index = index;
}

/**
 * Read DATA_PORT register
 * Reads a byte from the currently selected index with auto-stepping
 * 
 * @param window_num Window number (0-7 for Windows A-H)
 * @return Data byte read from the current index address
 */
static uint8_t read_data_port(uint8_t window_num) {
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
static void write_data_port(uint8_t window_num, uint8_t data) {
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
 * @param window_num Window number (0-7 for Windows A-H)
 * @return Currently selected configuration field
 */
static uint8_t read_cfg_field_select(uint8_t window_num) {
    // Return the selected configuration field for the specified window
    return g_window_state[window_num].config_field_select;
}

/**
 * Write CFG_FIELD_SELECT register
 * Updates the configuration field selection for the specified window
 * 
 * @param window_num Window number (0-7 for Windows A-H)
 * @param field Configuration field to select
 */
static void write_cfg_field_select(uint8_t window_num, uint8_t field) {
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

uint8_t bus_interface_read(uint8_t local_addr) {
    // Decode 8-bit local address
    bool is_shared = (local_addr & 0x80) != 0;
    uint8_t window_num = (local_addr >> 4) & 0x07;
    uint8_t reg_offset = is_shared ? local_addr & 0x7F : local_addr & 0x0F;
        
    // Check if accessing shared register space
    if (is_shared) {
        // Handle shared register reads
        switch (local_addr) {
            case REG_DEVICE_STATUS:
                // TODO: Implement DEVICE_STATUS read handler
                return 0x00;
                
            case REG_IRQ_CAUSE_LOW:
                // TODO: Implement IRQ_CAUSE_LOW read handler
                return 0x00;
                
            case REG_IRQ_CAUSE_HIGH:
                // TODO: Implement IRQ_CAUSE_HIGH read handler
                return 0x00;
                
            case REG_IRQ_MASK_LOW:
                // TODO: Implement IRQ_MASK_LOW read handler
                return 0xFF;  // Default: all interrupts enabled
                
            case REG_IRQ_MASK_HIGH:
                // TODO: Implement IRQ_MASK_HIGH read handler
                return 0xFF;  // Default: all interrupts enabled
                
            case REG_IRQ_ENABLE:
                // TODO: Implement IRQ_ENABLE read handler
                return 0x01;  // Default: interrupts enabled
                
            case REG_DEVICE_ID_LOW:
                // TODO: Implement DEVICE_ID_LOW read handler
                return 0x4D;  // 'M' for MIA
                
            case REG_DEVICE_ID_HIGH:
                // TODO: Implement DEVICE_ID_HIGH read handler
                return 0x49;  // 'I' for MIA
                
            default:
                // Reserved shared register
                return 0x00;
        }
    }
    
    // Handle window register reads
    // Route to appropriate register handler based on offset
    switch (reg_offset) {
        case REG_OFFSET_IDX_SELECT:
            return read_idx_select(window_num);
            
        case REG_OFFSET_DATA_PORT:
            return read_data_port(window_num);
            
        case REG_OFFSET_CFG_FIELD_SELECT:
            return read_cfg_field_select(window_num);
            
        case REG_OFFSET_CFG_DATA:
            return read_cfg_data(window_num);
            
        case REG_OFFSET_COMMAND:
            // Command register is write-only
            return 0x00;
            
        default:
            // Reserved register offset (5-15)
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
                // TODO: Implement IRQ_CAUSE_LOW write handler (write-1-to-clear)
                (void)data;
                break;
                
            case REG_IRQ_CAUSE_HIGH:
                // TODO: Implement IRQ_CAUSE_HIGH write handler (write-1-to-clear)
                (void)data;
                break;
                
            case REG_IRQ_MASK_LOW:
                // TODO: Implement IRQ_MASK_LOW write handler
                (void)data;
                break;
                
            case REG_IRQ_MASK_HIGH:
                // TODO: Implement IRQ_MASK_HIGH write handler
                (void)data;
                break;
                
            case REG_IRQ_ENABLE:
                // TODO: Implement IRQ_ENABLE write handler
                (void)data;
                break;
                
            case REG_DEVICE_ID_LOW:
            case REG_DEVICE_ID_HIGH:
                // Device ID is read-only
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
            // TODO: Implement COMMAND write handler
            break;
            
        default:
            // Reserved register offset (5-15) - ignore writes
            break;
    }
}
