/**
 * MIA Indexed Memory System
 * 
 * Provides 256 independent memory indexes with automatic stepping,
 * dual-window access, and DMA capabilities for the Clementina 6502 computer.
 */

#ifndef INDEXED_MEMORY_H
#define INDEXED_MEMORY_H

#include <stdint.h>
#include <stdbool.h>
#include "irq/irq.h"

// Inter-core command structure, used to queue DMA copy commands from Core 0 to Core 1
typedef struct {
    uint8_t src_idx;
    uint8_t dst_idx;
    uint16_t count;
} copy_command_t;

// Index allocation ranges
#define IDX_SYSTEM_ERROR        0
#define IDX_SYSTEM_START        1
#define IDX_SYSTEM_END          15
#define IDX_CHARACTER_START     16
#define IDX_CHARACTER_END       23      // 8 character tables (16-23)
#define IDX_PALETTE_START       32
#define IDX_PALETTE_END         47      // 16 palette banks (32-47)
#define IDX_NAMETABLE_START     48
#define IDX_NAMETABLE_END       51      // 4 nametables (48-51)
#define IDX_PALETTE_TABLE_START 52
#define IDX_PALETTE_TABLE_END   55      // 4 palette tables (52-55)
#define IDX_SPRITE_OAM          56      // Sprite OAM (256 sprites × 4 bytes)
#define IDX_ACTIVE_FRAME        57      // Active frame control (0 or 1 to select buffer set)
#define IDX_VIDEO_RESERVED_START 58
#define IDX_VIDEO_RESERVED_END  63      // Reserved for video expansion
#define IDX_USB_START           64
#define IDX_USB_END             79
#define IDX_SYSCTRL_START       80
#define IDX_SYSCTRL_END         95
#define IDX_RESERVED_START      96
#define IDX_RESERVED_END        127
#define IDX_USER_START          128
#define IDX_USER_END            255

// Configuration field IDs
#define CFG_ADDR_L              0x00
#define CFG_ADDR_M              0x01
#define CFG_ADDR_H              0x02
#define CFG_DEFAULT_L           0x03
#define CFG_DEFAULT_M           0x04
#define CFG_DEFAULT_H           0x05
#define CFG_LIMIT_L             0x06
#define CFG_LIMIT_M             0x07
#define CFG_LIMIT_H             0x08
#define CFG_STEP                0x09
#define CFG_FLAGS               0x0A
#define CFG_COPY_SRC_IDX        0x0B
#define CFG_COPY_DST_IDX        0x0C
#define CFG_COPY_COUNT_L        0x0D
#define CFG_COPY_COUNT_H        0x0E

// Flag bits
#define FLAG_AUTO_STEP          0x01
#define FLAG_DIRECTION          0x02    // 0=forward, 1=backward
#define FLAG_WRAP_ON_LIMIT      0x04    // 0=disabled, 1=wrap to default when reaching limit

// Window-level command codes (executed via window COMMAND register at +0x04)
// These commands operate on the currently selected index for that window
#define CMD_NOP                 0x00    // No operation
#define CMD_RESET_INDEX         0x01    // Reset current address to default address
#define CMD_SET_DEFAULT_TO_ADDR 0x02    // Set default address to current address
#define CMD_SET_LIMIT_TO_ADDR   0x03    // Set limit address to current address

// Shared/system-level command codes (executed via shared COMMAND register at 0xFF)
// These commands affect the entire system, not a specific window
#define CMD_SHARED_NOP              0x00    // No operation (shared)
#define CMD_RESET_ALL_IDX           0x01    // Reset all 256 indexes to default addresses
#define CMD_FACTORY_RESET_ALL_IDX   0x02    // Factory reset indexed memory subsystem
#define CMD_CLEAR_IRQ               0x03    // Clear all pending interrupts
#define CMD_COPY_BLOCK              0x04    // Execute DMA block copy
#define CMD_SYSTEM_RESET            0x05    // Full hardware reset (reboots Pico and 6502)

// Status bits (non-IRQ related)
#define STATUS_BUSY             0x01
#define STATUS_IRQ_PENDING      0x02    // For bus interface compatibility
#define STATUS_MEMORY_ERROR     0x04
#define STATUS_INDEX_OVERFLOW   0x08
#define STATUS_USB_DATA_READY   0x10
#define STATUS_VIDEO_FRAME_READY 0x20
#define STATUS_DMA_ACTIVE       0x40
#define STATUS_SYSTEM_READY     0x80

// Performance optimization macros
#define ADDR_VALID(addr) ((addr) < MIA_MEMORY_SIZE)
#define CHECK_ADDR_OR_RETURN(addr, retval) \
    if ((addr) >= MIA_MEMORY_SIZE) { \
        g_state.status |= STATUS_MEMORY_ERROR; \
        irq_set_bits(IRQ_MEMORY_ERROR); \
        return retval; \
    }

#define CHECK_ADDR_OR_RETURN_VOID(addr) \
    if ((addr) >= MIA_MEMORY_SIZE) { \
        g_state.status |= STATUS_MEMORY_ERROR; \
        irq_set_bits(IRQ_MEMORY_ERROR); \
        return; \
    }

// Address field type for generic setter
typedef enum {
    ADDR_CURRENT,
    ADDR_DEFAULT, 
    ADDR_LIMIT
} addr_field_t;

// Memory index structure (16 bytes per index)
typedef struct {
    uint32_t current_addr;      // 24-bit current address (upper 8 bits unused)
    uint32_t default_addr;      // 24-bit default address (upper 8 bits unused)
    uint32_t limit_addr;        // 24-bit limit address for wrap-on-limit (upper 8 bits unused)
    uint8_t step;              // Step size (0-255 bytes)
    uint8_t flags;             // Behavior flags (AUTO_STEP, DIRECTION, WRAP_ON_LIMIT)
    uint16_t reserved;         // Reserved for future use
} index_t;

// Global DMA configuration
typedef struct {
    uint8_t src_idx;
    uint8_t dst_idx;
    uint16_t count;
} dma_config_t;

// System state (non-IRQ related)
typedef struct {
    index_t indexes[256];       // 256 memory indexes
    dma_config_t dma_config;    // DMA operation configuration
    uint8_t status;            // System status register (non-IRQ bits)
} indexed_memory_state_t;

// Public API - functions used by other modules
void indexed_memory_init(void);

// Memory access
uint8_t indexed_memory_read(uint8_t idx);
void indexed_memory_write(uint8_t idx, uint8_t data);

// Configuration
uint8_t indexed_memory_get_config_field(uint8_t idx, uint8_t field);
void indexed_memory_set_config_field(uint8_t idx, uint8_t field, uint8_t value);

// Commands
void indexed_memory_execute_window_command(uint8_t idx, uint8_t cmd);
void indexed_memory_execute_shared_command(uint8_t cmd);

// Status management
void indexed_memory_set_status(uint8_t status_bits);
uint8_t indexed_memory_get_status(void);

// Core 1 processing
void indexed_memory_process_copy_command(void);

#endif // INDEXED_MEMORY_H