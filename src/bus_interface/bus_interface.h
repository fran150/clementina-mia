/**
 * MIA Bus Interface Module
 * 
 * Provides the 6502 bus interface for the indexed memory system.
 * Handles register access for dual-window architecture ($C000-$C00F).
 * 
 * Memory Map:
 * - Window A: $C000-$C007 (mirrored throughout $C000-$C3FF)
 * - Window B: $C008-$C00F (mirrored throughout $C000-$C3FF)
 * 
 * Register Layout (both windows):
 * - +0: IDX_SELECT - Select active index (0-255)
 * - +1: DATA_PORT - Read/write byte at current index address with auto-step
 * - +2: CFG_FIELD_SELECT - Select configuration field
 * - +3: CFG_DATA - Read/write selected configuration field
 * - +4: COMMAND - Issue control commands
 * - +5: STATUS - Device status bits (shared)
 * - +6: IRQ_CAUSE_LOW - Interrupt source identification low byte (shared)
 * - +7: IRQ_CAUSE_HIGH - Interrupt source identification high byte (shared)
 */

#ifndef BUS_INTERFACE_H
#define BUS_INTERFACE_H

#include <stdint.h>
#include <stdbool.h>

// ============================================================================
// Register Address Constants
// ============================================================================

// Base addresses for indexed interface mode
#define BUS_INTERFACE_BASE      0xC000  // Base address for indexed interface
#define BUS_INTERFACE_SIZE      0x0400  // 1KB address space ($C000-$C3FF)
#define BUS_INTERFACE_WINDOW    0x0010  // 16-byte register window

// Window A register offsets (absolute addresses)
#define REG_IDX_SELECT_A        0xC000  // Window A: Index selection
#define REG_DATA_PORT_A         0xC001  // Window A: Data port with auto-step
#define REG_CFG_FIELD_SELECT_A  0xC002  // Window A: Configuration field selector
#define REG_CFG_DATA_A          0xC003  // Window A: Configuration data
#define REG_COMMAND_A           0xC004  // Window A: Command register
#define REG_STATUS_A            0xC005  // Window A: Status register (shared)
#define REG_IRQ_CAUSE_LOW_A     0xC006  // Window A: IRQ cause low byte (shared)
#define REG_IRQ_CAUSE_HIGH_A    0xC007  // Window A: IRQ cause high byte (shared)

// Window B register offsets (absolute addresses)
#define REG_IDX_SELECT_B        0xC008  // Window B: Index selection
#define REG_DATA_PORT_B         0xC009  // Window B: Data port with auto-step
#define REG_CFG_FIELD_SELECT_B  0xC00A  // Window B: Configuration field selector
#define REG_CFG_DATA_B          0xC00B  // Window B: Configuration data
#define REG_COMMAND_B           0xC00C  // Window B: Command register
#define REG_STATUS_B            0xC00D  // Window B: Status register (shared)
#define REG_IRQ_CAUSE_LOW_B     0xC00E  // Window B: IRQ cause low byte (shared)
#define REG_IRQ_CAUSE_HIGH_B    0xC00F  // Window B: IRQ cause high byte (shared)

// Register offsets within 16-byte window (0-7 for each window)
#define REG_OFFSET_IDX_SELECT       0x00
#define REG_OFFSET_DATA_PORT        0x01
#define REG_OFFSET_CFG_FIELD_SELECT 0x02
#define REG_OFFSET_CFG_DATA         0x03
#define REG_OFFSET_COMMAND          0x04
#define REG_OFFSET_STATUS           0x05
#define REG_OFFSET_IRQ_CAUSE_LOW    0x06
#define REG_OFFSET_IRQ_CAUSE_HIGH   0x07

// ============================================================================
// Window Detection Macros
// ============================================================================

/**
 * Check if an address is within the indexed interface range
 * @param addr 16-bit address to check
 * @return true if address is in $C000-$C3FF range
 */
#define IS_INDEXED_INTERFACE_ADDR(addr) \
    (((addr) >= BUS_INTERFACE_BASE) && ((addr) < (BUS_INTERFACE_BASE + BUS_INTERFACE_SIZE)))

/**
 * Extract register offset from address (0-15)
 * The 16-byte register window is mirrored throughout the 1KB space
 * @param addr 16-bit address
 * @return Register offset (0-15)
 */
#define GET_REGISTER_OFFSET(addr) \
    ((addr) & 0x000F)

/**
 * Detect which window is being accessed
 * Window A: bit 3 = 0 (offsets 0-7)
 * Window B: bit 3 = 1 (offsets 8-15)
 * @param addr 16-bit address
 * @return true if Window B, false if Window A
 */
#define IS_WINDOW_B(addr) \
    ((GET_REGISTER_OFFSET(addr) & 0x08) != 0)

/**
 * Get register offset within window (0-7)
 * Strips the window bit to get the actual register offset
 * @param addr 16-bit address
 * @return Register offset within window (0-7)
 */
#define GET_WINDOW_REGISTER_OFFSET(addr) \
    (GET_REGISTER_OFFSET(addr) & 0x07)

// ============================================================================
// Helper Functions
// ============================================================================

/**
 * Check if address is in indexed interface range
 * @param address 16-bit address to check
 * @return true if address is in $C000-$C3FF range
 */
static inline bool bus_interface_is_indexed_addr(uint16_t address) {
    return IS_INDEXED_INTERFACE_ADDR(address);
}

/**
 * Detect which window is being accessed
 * @param address 16-bit address
 * @return true if Window B, false if Window A
 */
static inline bool bus_interface_is_window_b(uint16_t address) {
    return IS_WINDOW_B(address);
}

/**
 * Get register offset within window (0-7)
 * @param address 16-bit address
 * @return Register offset (0-7)
 */
static inline uint8_t bus_interface_get_register_offset(uint16_t address) {
    return GET_WINDOW_REGISTER_OFFSET(address);
}

// ============================================================================
// Main Entry Points
// ============================================================================

/**
 * Initialize the bus interface module
 * Sets up GPIO pins, PIO state machines, and internal state
 */
void bus_interface_init(void);

/**
 * Handle a READ operation from the 6502 bus
 * Called by PIO interrupt handler or main loop
 * 
 * @param address 16-bit address being read
 * @return Data byte to return to 6502
 */
uint8_t bus_interface_read(uint16_t address);

/**
 * Handle a WRITE operation from the 6502 bus
 * Called by PIO interrupt handler or main loop
 * 
 * @param address 16-bit address being written
 * @param data Data byte from 6502
 */
void bus_interface_write(uint16_t address, uint8_t data);

#endif // BUS_INTERFACE_H
