/**
 * MIA Bus Interface Module
 * 
 * Provides the 6502 bus interface for the indexed memory system.
 * Handles register access for multi-window architecture with shared registers.
 * 
 * Memory Map (8-bit address space: 0x00-0xFF):
 * - Window A: 0x00-0x0F (16 registers, 0-4 active, 5-15 reserved)
 * - Window B: 0x10-0x1F (16 registers, 0-4 active, 5-15 reserved)
 * - Window C: 0x20-0x2F (16 registers, 0-4 active, 5-15 reserved)
 * - Window D: 0x30-0x3F (16 registers, 0-4 active, 5-15 reserved)
 * - Reserved: 0x40-0x7F (future window expansion E-H)
 * - Shared:   0x80-0xFF (128 bytes, active from 0xF0-0xFF)
 * 
 * Register Layout (Windows A-D):
 * - +0: IDX_SELECT - Select active index (0-255)
 * - +1: DATA_PORT - Read/write byte at current index address with auto-step
 * - +2: CFG_FIELD_SELECT - Select configuration field
 * - +3: CFG_DATA - Read/write selected configuration field
 * - +4: COMMAND - Issue control commands
 * - +5-15: Reserved for future use
 * 
 * Shared Register Layout (0xF0-0xFF):
 * - 0xF0: DEVICE_STATUS - Global device status
 * - 0xF1: IRQ_CAUSE_LOW - Interrupt source low byte (bits 0-7)
 * - 0xF2: IRQ_CAUSE_HIGH - Interrupt source high byte (bits 8-15)
 * - 0xF3: IRQ_MASK_LOW - Interrupt mask low byte
 * - 0xF4: IRQ_MASK_HIGH - Interrupt mask high byte
 * - 0xF5: IRQ_ENABLE - Global interrupt enable
 * - 0xF6-0xFF: Reserved shared registers
 */

#ifndef BUS_INTERFACE_H
#define BUS_INTERFACE_H

#include <stdint.h>
#include <stdbool.h>

// ============================================================================
// Register Address Constants
// ============================================================================

// NOTE: MIA only sees 8 address lines (A0-A7 on GPIO 0-7)
// The 6502 address space $C000-$C0FF is selected by IO0_CS chip select line
// MIA sees only the lower 8 bits: 0x00-0xFF

// Window base addresses (8-bit local addresses)
#define WINDOW_A_BASE           0x00    // Window A: 0x00-0x0F
#define WINDOW_B_BASE           0x10    // Window B: 0x10-0x1F
#define WINDOW_C_BASE           0x20    // Window C: 0x20-0x2F (reserved)
#define WINDOW_D_BASE           0x30    // Window D: 0x30-0x3F (reserved)
#define SHARED_BASE             0x80    // Shared registers: 0x80-0xFF

// Active shared register addresses (0xF0-0xFF)
#define REG_DEVICE_STATUS       0xF0    // Shared: Global device status
#define REG_IRQ_CAUSE_LOW       0xF1    // Shared: Interrupt source low byte
#define REG_IRQ_CAUSE_HIGH      0xF2    // Shared: Interrupt source high byte
#define REG_IRQ_MASK_LOW        0xF3    // Shared: Interrupt mask low byte
#define REG_IRQ_MASK_HIGH       0xF4    // Shared: Interrupt mask high byte
#define REG_IRQ_ENABLE          0xF5    // Shared: Global interrupt enable
// 0xF6-0xFE: Reserved shared registers
#define REG_SHARED_COMMAND      0xFF    // Shared: System-wide command register

// Register offsets within 16-byte window (0-15 for each window)
#define REG_OFFSET_IDX_SELECT       0x00
#define REG_OFFSET_DATA_PORT        0x01
#define REG_OFFSET_CFG_FIELD_SELECT 0x02
#define REG_OFFSET_CFG_DATA         0x03
#define REG_OFFSET_COMMAND          0x04
// Offsets 0x05-0x0F are reserved for future use

// ============================================================================
// Address Decoding (8-bit local addresses)
// ============================================================================

/**
 * Address decoding is performed inline in bus_interface_read/write:
 * 
 * Shared space detection:
 *   is_shared = (local_addr & 0x80) != 0
 *   Bit 7 set indicates shared register space (0x80-0xFF)
 * 
 * Window number extraction:
 *   window_num = (local_addr >> 4) & 0x07
 *   Bits 4-6 determine window: 0=A, 1=B, 2=C, 3=D, 4-7=future
 * 
 * Register offset extraction:
 *   reg_offset = is_shared ? (local_addr & 0x7F) : (local_addr & 0x0F)
 *   Shared space: bits 0-6 (0-127)
 *   Window space: bits 0-3 (0-15)
 */


// ============================================================================
// Window State Management
// ============================================================================

// Maximum number of windows supported (A-H = 8 windows)
#define MAX_WINDOWS 8

/**
 * Window state structure
 * Tracks per-window state for independent operation
 */
typedef struct {
    uint8_t active_index;           // Currently selected index (0-255) for this window
    uint8_t config_field_select;    // Selected configuration field for this window
} window_state_t;

/**
 * Global window state array
 * Direct access for efficiency - window_num is always valid (0-7) from address decoding
 * Usage: g_window_state[window_num].active_index = idx;
 */
extern window_state_t g_window_state[MAX_WINDOWS];

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
 * NOTE: MIA only sees 8-bit addresses (A0-A7 on GPIO 0-7)
 * The IO0_CS chip select line indicates we're in indexed interface mode
 * 
 * @param local_addr 8-bit local address (what MIA sees on GPIO 0-7)
 * @return Data byte to return to 6502
 */
uint8_t bus_interface_read(uint8_t local_addr);

/**
 * Handle a WRITE operation from the 6502 bus
 * Called by PIO interrupt handler or main loop
 * 
 * NOTE: MIA only sees 8-bit addresses (A0-A7 on GPIO 0-7)
 * The IO0_CS chip select line indicates we're in indexed interface mode
 * 
 * @param local_addr 8-bit local address (what MIA sees on GPIO 0-7)
 * @param data Data byte from 6502
 */
void bus_interface_write(uint8_t local_addr, uint8_t data);

#endif // BUS_INTERFACE_H
