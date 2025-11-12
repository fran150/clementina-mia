/**
 * MIA Indexed Memory System Implementation
 * 
 * Core implementation of the 256-index memory system with automatic stepping,
 * configuration management, and DMA capabilities.
 */

#include "indexed_memory.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// MIA memory layout (256KB allocated from 520KB SRAM)
#define MIA_MEMORY_BASE         0x20000000
#define MIA_INDEX_TABLE_BASE    (MIA_MEMORY_BASE + 0x00000000)  // 2KB
#define MIA_SYSTEM_AREA_BASE    (MIA_MEMORY_BASE + 0x00000800)  // 16KB
#define MIA_VIDEO_AREA_BASE     (MIA_MEMORY_BASE + 0x00004800)  // 60KB
#define MIA_USER_AREA_BASE      (MIA_MEMORY_BASE + 0x00013800)  // 162KB
#define MIA_IO_BUFFER_BASE      (MIA_MEMORY_BASE + 0x0003C000)  // 16KB
#define MIA_MEMORY_SIZE         0x40000  // 256KB total MIA memory

// For host testing, convert hardware addresses to offsets
#ifndef PICO_BUILD
#define MIA_ADDR_TO_OFFSET(addr) ((addr) - MIA_MEMORY_BASE)
#define MIA_SYSTEM_AREA_OFFSET  MIA_ADDR_TO_OFFSET(MIA_SYSTEM_AREA_BASE)
#define MIA_VIDEO_AREA_OFFSET   MIA_ADDR_TO_OFFSET(MIA_VIDEO_AREA_BASE)
#define MIA_USER_AREA_OFFSET    MIA_ADDR_TO_OFFSET(MIA_USER_AREA_BASE)
#define MIA_IO_BUFFER_OFFSET    MIA_ADDR_TO_OFFSET(MIA_IO_BUFFER_BASE)
#endif

// Global system state
static indexed_memory_state_t g_state;

// Memory area pointers for validation
#ifdef PICO_BUILD
// On hardware, use actual SRAM address
static uint8_t* const mia_memory = (uint8_t*)MIA_MEMORY_BASE;
#else
// On host, allocate memory dynamically
static uint8_t* mia_memory = NULL;
#endif

/**
 * Initialize the indexed memory system
 */
void indexed_memory_init(void) {
#ifndef PICO_BUILD
    // On host, allocate memory if not already allocated
    if (mia_memory == NULL) {
        mia_memory = (uint8_t*)calloc(MIA_MEMORY_SIZE, 1);
        if (mia_memory == NULL) {
            printf("ERROR: Failed to allocate MIA memory for testing\n");
            return;
        }
    }
#endif
    
    // Clear all state
    memset(&g_state, 0, sizeof(g_state));
    
    // Initialize system status
    g_state.status = STATUS_SYSTEM_READY;
    g_state.irq_cause = IRQ_NO_IRQ;
    g_state.irq_mask = 0xFFFF; // All interrupts enabled by default (16-bit)
    
    // Pre-configure system indexes
    
    // Index 0: System error log
    indexed_memory_set_index_address(IDX_SYSTEM_ERROR, MIA_SYSTEM_AREA_BASE);
    indexed_memory_set_index_default(IDX_SYSTEM_ERROR, MIA_SYSTEM_AREA_BASE);
    indexed_memory_set_index_step(IDX_SYSTEM_ERROR, 1);
    indexed_memory_set_index_flags(IDX_SYSTEM_ERROR, FLAG_AUTO_STEP);
    
    // Character tables (indexes 16-23) - 8 tables, shared by background and sprites
    for (int i = 0; i < 8; i++) {
        uint8_t idx = IDX_CHARACTER_START + i;
        uint32_t addr = MIA_VIDEO_AREA_BASE + (i * 256 * 24); // 256 chars × 24 bytes each
        indexed_memory_set_index_address(idx, addr);
        indexed_memory_set_index_default(idx, addr);
        indexed_memory_set_index_limit(idx, addr + (256 * 24)); // Wrap at end of character table (6KB)
        indexed_memory_set_index_step(idx, 1);
        indexed_memory_set_index_flags(idx, FLAG_AUTO_STEP | FLAG_WRAP_ON_LIMIT);
    }
    
    // Palette banks (indexes 32-47) - 16 banks, shared resource
    uint32_t palette_base = MIA_VIDEO_AREA_BASE + (8 * 256 * 24); // After 8 char tables (48KB)
    for (int i = 0; i < 16; i++) {
        uint8_t idx = IDX_PALETTE_START + i;
        uint32_t addr = palette_base + (i * 16); // 8 colors × 2 bytes per bank
        indexed_memory_set_index_address(idx, addr);
        indexed_memory_set_index_default(idx, addr);
        indexed_memory_set_index_limit(idx, addr + 16); // Wrap at end of palette bank (16 bytes)
        indexed_memory_set_index_step(idx, 1);
        indexed_memory_set_index_flags(idx, FLAG_AUTO_STEP | FLAG_WRAP_ON_LIMIT);
    }
    
    // Nametables (indexes 48-51) - 4 tables for double buffering and scrolling
    uint32_t nametable_base = palette_base + (16 * 16); // After palette banks (256 bytes)
    for (int i = 0; i < 4; i++) {
        uint8_t idx = IDX_NAMETABLE_START + i;
        uint32_t addr = nametable_base + (i * 40 * 25); // 40×25 bytes per nametable
        indexed_memory_set_index_address(idx, addr);
        indexed_memory_set_index_default(idx, addr);
        indexed_memory_set_index_limit(idx, addr + (40 * 25)); // Wrap at end of nametable (1000 bytes)
        indexed_memory_set_index_step(idx, 1);
        indexed_memory_set_index_flags(idx, FLAG_AUTO_STEP | FLAG_WRAP_ON_LIMIT);
    }
    
    // Palette tables (indexes 52-55) - 4 tables for double buffering and scrolling
    uint32_t palette_table_base = nametable_base + (4 * 40 * 25); // After nametables (4KB)
    for (int i = 0; i < 4; i++) {
        uint8_t idx = IDX_PALETTE_TABLE_START + i;
        uint32_t addr = palette_table_base + (i * 40 * 25); // 40×25 bytes per palette table
        indexed_memory_set_index_address(idx, addr);
        indexed_memory_set_index_default(idx, addr);
        indexed_memory_set_index_limit(idx, addr + (40 * 25)); // Wrap at end of palette table (1000 bytes)
        indexed_memory_set_index_step(idx, 1);
        indexed_memory_set_index_flags(idx, FLAG_AUTO_STEP | FLAG_WRAP_ON_LIMIT);
    }
    
    // Sprite OAM (index 56) - 256 sprites × 4 bytes, sprites use character table graphics
    uint32_t sprite_oam_base = palette_table_base + (4 * 40 * 25); // After palette tables (4KB)
    indexed_memory_set_index_address(IDX_SPRITE_OAM, sprite_oam_base);
    indexed_memory_set_index_default(IDX_SPRITE_OAM, sprite_oam_base);
    indexed_memory_set_index_limit(IDX_SPRITE_OAM, sprite_oam_base + (256 * 4)); // Wrap at end of OAM (1024 bytes)
    indexed_memory_set_index_step(IDX_SPRITE_OAM, 4); // Step by sprite record size (4 bytes)
    indexed_memory_set_index_flags(IDX_SPRITE_OAM, FLAG_AUTO_STEP | FLAG_WRAP_ON_LIMIT);
    
    // Active frame control (index 57) - selects which buffer set (0 or 1) for video transmission
    uint32_t active_frame_base = sprite_oam_base + (256 * 4); // After sprite OAM (1KB)
    indexed_memory_set_index_address(IDX_ACTIVE_FRAME, active_frame_base);
    indexed_memory_set_index_default(IDX_ACTIVE_FRAME, active_frame_base);
    indexed_memory_set_index_step(IDX_ACTIVE_FRAME, 1);
    indexed_memory_set_index_flags(IDX_ACTIVE_FRAME, 0); // No auto-step
    
    // USB keyboard buffer (indexes 64-79)
    uint32_t usb_base = MIA_IO_BUFFER_BASE;
    
    // Index 64: USB keyboard circular buffer
    indexed_memory_set_index_address(64, usb_base);
    indexed_memory_set_index_default(64, usb_base);
    indexed_memory_set_index_limit(64, usb_base + 64); // Wrap at end of keyboard buffer (64 bytes)
    indexed_memory_set_index_step(64, 1);
    indexed_memory_set_index_flags(64, FLAG_AUTO_STEP | FLAG_WRAP_ON_LIMIT);
    
    // Index 65: USB status
    indexed_memory_set_index_address(65, usb_base + 64);
    indexed_memory_set_index_default(65, usb_base + 64);
    indexed_memory_set_index_step(65, 1);
    indexed_memory_set_index_flags(65, 0); // No auto-step for status
    
    // System control (indexes 80-95)
    uint32_t sysctrl_base = MIA_SYSTEM_AREA_BASE + 0x1000;
    
    // Index 80: Clock control
    indexed_memory_set_index_address(80, sysctrl_base);
    indexed_memory_set_index_default(80, sysctrl_base);
    indexed_memory_set_index_step(80, 1);
    indexed_memory_set_index_flags(80, 0);
    
    // Index 81: Reset control
    indexed_memory_set_index_address(81, sysctrl_base + 16);
    indexed_memory_set_index_default(81, sysctrl_base + 16);
    indexed_memory_set_index_step(81, 1);
    indexed_memory_set_index_flags(81, 0);
    
    // Index 83: IRQ mask control low byte (enable/disable interrupt sources 0-7)
    indexed_memory_set_index_address(83, sysctrl_base + 48);
    indexed_memory_set_index_default(83, sysctrl_base + 48);
    indexed_memory_set_index_step(83, 1);
    indexed_memory_set_index_flags(83, 0);
    
    // Index 84: IRQ mask control high byte (enable/disable interrupt sources 8-15)
    indexed_memory_set_index_address(84, sysctrl_base + 49);
    indexed_memory_set_index_default(84, sysctrl_base + 49);
    indexed_memory_set_index_step(84, 1);
    indexed_memory_set_index_flags(84, 0);
    
    // User area (indexes 128-255) - 162KB of user RAM
    // All user indexes start at the base of user memory
    // Users will reconfigure as needed for their applications
    for (int i = IDX_USER_START; i <= IDX_USER_END; i++) {
        indexed_memory_set_index_address(i, MIA_USER_AREA_BASE);
        indexed_memory_set_index_default(i, MIA_USER_AREA_BASE);
        indexed_memory_set_index_step(i, 1);
        indexed_memory_set_index_flags(i, FLAG_AUTO_STEP);
    }
    
    printf("Indexed memory system initialized with 256 indexes\n");
}

/**
 * Reset all indexes to their default addresses
 */
void indexed_memory_reset_all(void) {
    for (int i = 0; i < 256; i++) {
        indexed_memory_reset_index(i);
    }
}

/**
 * Set index current address
 */
void indexed_memory_set_index_address(uint8_t idx, uint32_t address) {
    g_state.indexes[idx].current_addr = address & 0xFFFFFF;
}

/**
 * Set index default address
 */
void indexed_memory_set_index_default(uint8_t idx, uint32_t address) {
    g_state.indexes[idx].default_addr = address & 0xFFFFFF;
}

/**
 * Set index limit address (for wrap-on-limit feature)
 */
void indexed_memory_set_index_limit(uint8_t idx, uint32_t address) {
    g_state.indexes[idx].limit_addr = address & 0xFFFFFF;
}

/**
 * Set index step size
 */
void indexed_memory_set_index_step(uint8_t idx, uint8_t step) {
    g_state.indexes[idx].step = step;
}

/**
 * Set index flags
 */
void indexed_memory_set_index_flags(uint8_t idx, uint8_t flags) {
    g_state.indexes[idx].flags = flags;
}

/**
 * Reset index to default address
 */
void indexed_memory_reset_index(uint8_t idx) {
    g_state.indexes[idx].current_addr = g_state.indexes[idx].default_addr;
}

/**
 * Read byte from index with auto-stepping
 */
uint8_t indexed_memory_read(uint8_t idx) {
    uint32_t addr = g_state.indexes[idx].current_addr;
    uint8_t flags = g_state.indexes[idx].flags;
    
    // Wrap address to valid range using bitwise AND (fast, since size is power of 2)
    // Works for both hardware (subtracts base) and host (same calculation)
    uint32_t offset = (addr - MIA_MEMORY_BASE) & (MIA_MEMORY_SIZE - 1);
    
    // Read data
    uint8_t data = mia_memory[offset];
    
    // Auto-step if enabled
    if (flags & FLAG_AUTO_STEP) {
        uint8_t step = g_state.indexes[idx].step;
        if (flags & FLAG_DIRECTION) {
            // Backward stepping
            addr -= step;
            
            // Check wrap-on-limit if enabled (for backward, wrap when going below limit)
            if (flags & FLAG_WRAP_ON_LIMIT) {
                if (addr < g_state.indexes[idx].limit_addr) {
                    addr = g_state.indexes[idx].default_addr;
                }
            }
        } else {
            // Forward stepping
            addr += step;
            
            // Check wrap-on-limit if enabled (for forward, wrap when reaching or exceeding limit)
            if (flags & FLAG_WRAP_ON_LIMIT) {
                if (addr >= g_state.indexes[idx].limit_addr) {
                    addr = g_state.indexes[idx].default_addr;
                }
            }
        }
        
        // Update address
        g_state.indexes[idx].current_addr = addr;
    }
    
    return data;
}

/**
 * Write byte to index with auto-stepping
 */
void indexed_memory_write(uint8_t idx, uint8_t data) {
    uint32_t addr = g_state.indexes[idx].current_addr;
    uint8_t flags = g_state.indexes[idx].flags;
    
    // Wrap address to valid range using bitwise AND (fast, since size is power of 2)
    // Works for both hardware (subtracts base) and host (same calculation)
    uint32_t offset = (addr - MIA_MEMORY_BASE) & (MIA_MEMORY_SIZE - 1);
    
    // Write data
    mia_memory[offset] = data;
    
    // Auto-step if enabled
    if (flags & FLAG_AUTO_STEP) {
        uint8_t step = g_state.indexes[idx].step;
        if (flags & FLAG_DIRECTION) {
            // Backward stepping
            addr -= step;
            
            // Check wrap-on-limit if enabled (for backward, wrap when going below limit)
            if (flags & FLAG_WRAP_ON_LIMIT) {
                if (addr < g_state.indexes[idx].limit_addr) {
                    addr = g_state.indexes[idx].default_addr;
                }
            }
        } else {
            // Forward stepping
            addr += step;
            
            // Check wrap-on-limit if enabled (for forward, wrap when reaching or exceeding limit)
            if (flags & FLAG_WRAP_ON_LIMIT) {
                if (addr >= g_state.indexes[idx].limit_addr) {
                    addr = g_state.indexes[idx].default_addr;
                }
            }
        }
        
        // Update address
        g_state.indexes[idx].current_addr = addr;
    }
}

/**
 * Read byte from index without auto-stepping
 */
uint8_t indexed_memory_read_no_step(uint8_t idx) {
    uint32_t addr = g_state.indexes[idx].current_addr;
    
    // Wrap address to valid range using bitwise AND (fast, since size is power of 2)
    // Works for both hardware (subtracts base) and host (same calculation)
    uint32_t offset = (addr - MIA_MEMORY_BASE) & (MIA_MEMORY_SIZE - 1);
    
    return mia_memory[offset];
}

/**
 * Write byte to index without auto-stepping
 */
void indexed_memory_write_no_step(uint8_t idx, uint8_t data) {
    uint32_t addr = g_state.indexes[idx].current_addr;
    
    // Wrap address to valid range using bitwise AND (fast, since size is power of 2)
    // Works for both hardware (subtracts base) and host (same calculation)
    uint32_t offset = (addr - MIA_MEMORY_BASE) & (MIA_MEMORY_SIZE - 1);
    
    mia_memory[offset] = data;
}

/**
 * Get configuration field value
 */
uint8_t indexed_memory_get_config_field(uint8_t idx, uint8_t field) {
    switch (field) {
        case CFG_ADDR_L:
            return g_state.indexes[idx].current_addr & 0xFF;
        case CFG_ADDR_M:
            return (g_state.indexes[idx].current_addr >> 8) & 0xFF;
        case CFG_ADDR_H:
            return (g_state.indexes[idx].current_addr >> 16) & 0xFF;
        case CFG_DEFAULT_L:
            return g_state.indexes[idx].default_addr & 0xFF;
        case CFG_DEFAULT_M:
            return (g_state.indexes[idx].default_addr >> 8) & 0xFF;
        case CFG_DEFAULT_H:
            return (g_state.indexes[idx].default_addr >> 16) & 0xFF;
        case CFG_LIMIT_L:
            return g_state.indexes[idx].limit_addr & 0xFF;
        case CFG_LIMIT_M:
            return (g_state.indexes[idx].limit_addr >> 8) & 0xFF;
        case CFG_LIMIT_H:
            return (g_state.indexes[idx].limit_addr >> 16) & 0xFF;
        case CFG_STEP:
            return g_state.indexes[idx].step;
        case CFG_FLAGS:
            return g_state.indexes[idx].flags;
        case CFG_COPY_SRC_IDX:
            return g_state.dma_config.src_idx;
        case CFG_COPY_DST_IDX:
            return g_state.dma_config.dst_idx;
        case CFG_COPY_COUNT_L:
            return g_state.dma_config.count & 0xFF;
        case CFG_COPY_COUNT_H:
            return (g_state.dma_config.count >> 8) & 0xFF;
        default:
            return 0;
    }
}

/**
 * Set configuration field value
 */
void indexed_memory_set_config_field(uint8_t idx, uint8_t field, uint8_t value) {
    switch (field) {
        case CFG_ADDR_L:
            g_state.indexes[idx].current_addr = (g_state.indexes[idx].current_addr & 0xFFFF00) | value;
            break;
        case CFG_ADDR_M:
            g_state.indexes[idx].current_addr = (g_state.indexes[idx].current_addr & 0xFF00FF) | (value << 8);
            break;
        case CFG_ADDR_H:
            g_state.indexes[idx].current_addr = (g_state.indexes[idx].current_addr & 0x00FFFF) | (value << 16);
            break;
        case CFG_DEFAULT_L:
            g_state.indexes[idx].default_addr = (g_state.indexes[idx].default_addr & 0xFFFF00) | value;
            break;
        case CFG_DEFAULT_M:
            g_state.indexes[idx].default_addr = (g_state.indexes[idx].default_addr & 0xFF00FF) | (value << 8);
            break;
        case CFG_DEFAULT_H:
            g_state.indexes[idx].default_addr = (g_state.indexes[idx].default_addr & 0x00FFFF) | (value << 16);
            break;
        case CFG_LIMIT_L:
            g_state.indexes[idx].limit_addr = (g_state.indexes[idx].limit_addr & 0xFFFF00) | value;
            break;
        case CFG_LIMIT_M:
            g_state.indexes[idx].limit_addr = (g_state.indexes[idx].limit_addr & 0xFF00FF) | (value << 8);
            break;
        case CFG_LIMIT_H:
            g_state.indexes[idx].limit_addr = (g_state.indexes[idx].limit_addr & 0x00FFFF) | (value << 16);
            break;
        case CFG_STEP:
            g_state.indexes[idx].step = value;
            break;
        case CFG_FLAGS:
            g_state.indexes[idx].flags = value;
            break;
        case CFG_COPY_SRC_IDX:
            g_state.dma_config.src_idx = value;
            break;
        case CFG_COPY_DST_IDX:
            g_state.dma_config.dst_idx = value;
            break;
        case CFG_COPY_COUNT_L:
            g_state.dma_config.count = (g_state.dma_config.count & 0xFF00) | value;
            break;
        case CFG_COPY_COUNT_H:
            g_state.dma_config.count = (g_state.dma_config.count & 0x00FF) | (value << 8);
            break;
    }
}

/**
 * Execute command
 */
void indexed_memory_execute_command(uint8_t cmd) {
    switch (cmd) {
        case CMD_NOP:
            break;
        case CMD_RESET_INDEX:
            indexed_memory_reset_index(g_state.window_a_idx);
            break;
        case CMD_RESET_ALL:
            indexed_memory_reset_all();
            break;
        case CMD_CLEAR_IRQ:
            indexed_memory_clear_irq();
            break;
        case CMD_COPY_BYTE:
            indexed_memory_copy_byte(g_state.dma_config.src_idx, g_state.dma_config.dst_idx);
            break;
        case CMD_COPY_BLOCK:
            indexed_memory_copy_block(g_state.dma_config.src_idx, g_state.dma_config.dst_idx, g_state.dma_config.count);
            break;
        case CMD_SET_COPY_SRC:
            g_state.dma_config.src_idx = g_state.window_a_idx;
            break;
        case CMD_SET_COPY_DST:
            g_state.dma_config.dst_idx = g_state.window_a_idx;
            break;
        case CMD_PICO_REINIT:
            indexed_memory_init();
            break;
        default:
            // Unknown command - could trigger system command handlers
            break;
    }
}

/**
 * Copy single byte between indexes
 * Uses auto-stepping versions to respect index configurations
 */
void indexed_memory_copy_byte(uint8_t src_idx, uint8_t dst_idx) {
    // Save current flags
    uint8_t src_flags = g_state.indexes[src_idx].flags;
    uint8_t dst_flags = g_state.indexes[dst_idx].flags;
    
    // Temporarily enable auto-step for both indexes
    g_state.indexes[src_idx].flags |= FLAG_AUTO_STEP;
    g_state.indexes[dst_idx].flags |= FLAG_AUTO_STEP;
    
    // Perform copy with auto-stepping
    uint8_t data = indexed_memory_read(src_idx);
    indexed_memory_write(dst_idx, data);
    
    // Restore original flags
    g_state.indexes[src_idx].flags = src_flags;
    g_state.indexes[dst_idx].flags = dst_flags;
}

/**
 * Copy block of bytes between indexes
 * Uses auto-stepping versions to respect index configurations
 */
void indexed_memory_copy_block(uint8_t src_idx, uint8_t dst_idx, uint16_t count) {
    g_state.status |= STATUS_DMA_ACTIVE;
    
    // Save current flags
    uint8_t src_flags = g_state.indexes[src_idx].flags;
    uint8_t dst_flags = g_state.indexes[dst_idx].flags;
    
    // Temporarily enable auto-step for both indexes
    g_state.indexes[src_idx].flags |= FLAG_AUTO_STEP;
    g_state.indexes[dst_idx].flags |= FLAG_AUTO_STEP;
    
    for (uint16_t i = 0; i < count; i++) {
        uint8_t data = indexed_memory_read(src_idx);
        indexed_memory_write(dst_idx, data);
    }
    
    // Restore original flags
    g_state.indexes[src_idx].flags = src_flags;
    g_state.indexes[dst_idx].flags = dst_flags;
    
    g_state.status &= ~STATUS_DMA_ACTIVE;
    indexed_memory_set_irq(IRQ_DMA_COMPLETE);
}

/**
 * Get system status
 */
uint8_t indexed_memory_get_status(void) {
    return g_state.status;
}

/**
 * Get IRQ cause (full 16-bit value)
 */
uint16_t indexed_memory_get_irq_cause(void) {
    return g_state.irq_cause;
}

/**
 * Get IRQ cause low byte (bits 0-7)
 */
uint8_t indexed_memory_get_irq_cause_low(void) {
    return g_state.irq_cause & 0xFF;
}

/**
 * Get IRQ cause high byte (bits 8-15)
 */
uint8_t indexed_memory_get_irq_cause_high(void) {
    return (g_state.irq_cause >> 8) & 0xFF;
}

/**
 * Write to IRQ cause low byte (write-1-to-clear)
 * Writing 1 to a bit position clears that interrupt
 */
void indexed_memory_write_irq_cause_low(uint8_t clear_bits) {
    // Clear bits where clear_bits has 1s (write-1-to-clear)
    g_state.irq_cause &= ~((uint16_t)clear_bits);
    
    // If no enabled interrupts are pending, deassert IRQ line
    if ((g_state.irq_cause & g_state.irq_mask) == 0) {
        g_state.status &= ~STATUS_IRQ_PENDING;
        // TODO: Deassert GPIO 26 (IRQ line) to 6502
    }
}

/**
 * Write to IRQ cause high byte (write-1-to-clear)
 * Writing 1 to a bit position clears that interrupt
 */
void indexed_memory_write_irq_cause_high(uint8_t clear_bits) {
    // Clear bits where clear_bits has 1s (write-1-to-clear)
    // Shift clear_bits to high byte position
    g_state.irq_cause &= ~((uint16_t)clear_bits << 8);
    
    // If no enabled interrupts are pending, deassert IRQ line
    if ((g_state.irq_cause & g_state.irq_mask) == 0) {
        g_state.status &= ~STATUS_IRQ_PENDING;
        // TODO: Deassert GPIO 26 (IRQ line) to 6502
    }
}

/**
 * Clear IRQ
 * Clears all pending interrupts
 */
void indexed_memory_clear_irq(void) {
    // Clear all pending interrupts
    g_state.irq_cause = IRQ_NO_IRQ;
    g_state.status &= ~STATUS_IRQ_PENDING;
    // TODO: Deassert GPIO 26 (IRQ line) to 6502
}

/**
 * Clear specific IRQ
 * Clears the specified interrupt bit(s) from the pending register
 * If no enabled interrupts remain pending, deasserts the IRQ line
 */
void indexed_memory_clear_specific_irq(uint16_t cause) {
    // Clear the specified interrupt bit(s)
    g_state.irq_cause &= ~cause;
    
    // If no enabled interrupts are pending, clear the IRQ line
    if ((g_state.irq_cause & g_state.irq_mask) == 0) {
        g_state.status &= ~STATUS_IRQ_PENDING;
        // TODO: Deassert GPIO 26 (IRQ line) to 6502
    }
}

/**
 * Set IRQ
 * Sets the specified interrupt bit(s) in the pending register
 * Only asserts IRQ line if the interrupt source is enabled in the mask
 */
void indexed_memory_set_irq(uint16_t cause) {
    // Set the interrupt bit(s) in the pending register (OR to accumulate)
    g_state.irq_cause |= cause;
    
    // Check if this interrupt source is enabled in the 16-bit mask
    // Only assert IRQ line if at least one enabled interrupt is pending
    if ((g_state.irq_cause & g_state.irq_mask) != 0) {
        g_state.status |= STATUS_IRQ_PENDING;
        // TODO: Assert GPIO 26 (IRQ line) to 6502
    }
}

/**
 * Set window index selection
 */
void indexed_memory_set_window_index(bool window_b, uint8_t idx) {
    if (window_b) {
        g_state.window_b_idx = idx;
    } else {
        g_state.window_a_idx = idx;
    }
}

/**
 * Get window index selection
 */
uint8_t indexed_memory_get_window_index(bool window_b) {
    return window_b ? g_state.window_b_idx : g_state.window_a_idx;
}

/**
 * Set configuration field selection
 */
void indexed_memory_set_config_field_select(bool window_b, uint8_t field) {
    if (window_b) {
        g_state.cfg_field_b = field;
    } else {
        g_state.cfg_field_a = field;
    }
}

/**
 * Get configuration field selection
 */
uint8_t indexed_memory_get_config_field_select(bool window_b) {
    return window_b ? g_state.cfg_field_b : g_state.cfg_field_a;
}

