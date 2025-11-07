/**
 * MIA Indexed Memory System Implementation
 * 
 * Core implementation of the 256-index memory system with automatic stepping,
 * configuration management, and DMA capabilities.
 */

#include "indexed_memory.h"
#include <string.h>
#include <stdio.h>

// MIA memory layout (520KB SRAM)
#define MIA_MEMORY_BASE         0x20000000
#define MIA_INDEX_TABLE_BASE    (MIA_MEMORY_BASE + 0x00000000)  // 2KB
#define MIA_SYSTEM_AREA_BASE    (MIA_MEMORY_BASE + 0x00000800)  // 30KB
#define MIA_VIDEO_AREA_BASE     (MIA_MEMORY_BASE + 0x00010000)  // 192KB
#define MIA_USER_AREA_BASE      (MIA_MEMORY_BASE + 0x00040000)  // 192KB
#define MIA_IO_BUFFER_BASE      (MIA_MEMORY_BASE + 0x00070000)  // 64KB
#define MIA_MEMORY_SIZE         0x80000  // 512KB usable

// Global system state
static indexed_memory_state_t g_state;

// Memory area pointers for validation
static uint8_t* const mia_memory = (uint8_t*)MIA_MEMORY_BASE;

/**
 * Initialize the indexed memory system
 */
void indexed_memory_init(void) {
    // Clear all state
    memset(&g_state, 0, sizeof(g_state));
    
    // Initialize system status
    g_state.status = STATUS_SYSTEM_READY;
    g_state.irq_cause = IRQ_NO_IRQ;
    
    // Pre-configure system indexes
    
    // Index 0: System error log
    indexed_memory_set_index_address(IDX_SYSTEM_ERROR, MIA_SYSTEM_AREA_BASE);
    indexed_memory_set_index_default(IDX_SYSTEM_ERROR, MIA_SYSTEM_AREA_BASE);
    indexed_memory_set_index_step(IDX_SYSTEM_ERROR, 1);
    indexed_memory_set_index_flags(IDX_SYSTEM_ERROR, FLAG_AUTO_STEP);
    
    // Character tables (indexes 16-31)
    for (int i = 0; i < 16; i++) {
        uint8_t idx = IDX_CHARACTER_START + i;
        uint32_t addr = MIA_VIDEO_AREA_BASE + (i * 256 * 24); // 256 chars × 24 bytes each
        indexed_memory_set_index_address(idx, addr);
        indexed_memory_set_index_default(idx, addr);
        indexed_memory_set_index_step(idx, 1);
        indexed_memory_set_index_flags(idx, FLAG_AUTO_STEP);
    }
    
    // Palette banks (indexes 32-47)
    for (int i = 0; i < 16; i++) {
        uint8_t idx = IDX_PALETTE_START + i;
        uint32_t addr = MIA_VIDEO_AREA_BASE + 0x18000 + (i * 16); // After char tables, 8 colors × 2 bytes
        indexed_memory_set_index_address(idx, addr);
        indexed_memory_set_index_default(idx, addr);
        indexed_memory_set_index_step(idx, 1);
        indexed_memory_set_index_flags(idx, FLAG_AUTO_STEP);
    }
    
    // Sprite/OAM data (indexes 48-63)
    uint32_t sprite_base = MIA_VIDEO_AREA_BASE + 0x19000;
    
    // Index 48: Sprite OAM (256 sprites × 4 bytes)
    indexed_memory_set_index_address(48, sprite_base);
    indexed_memory_set_index_default(48, sprite_base);
    indexed_memory_set_index_step(48, 4); // Step by sprite record size
    indexed_memory_set_index_flags(48, FLAG_AUTO_STEP);
    
    // Index 49: Sprite tile data
    indexed_memory_set_index_address(49, sprite_base + 0x400);
    indexed_memory_set_index_default(49, sprite_base + 0x400);
    indexed_memory_set_index_step(49, 1);
    indexed_memory_set_index_flags(49, FLAG_AUTO_STEP);
    
    // USB keyboard buffer (indexes 64-79)
    uint32_t usb_base = MIA_IO_BUFFER_BASE;
    
    // Index 64: USB keyboard circular buffer
    indexed_memory_set_index_address(64, usb_base);
    indexed_memory_set_index_default(64, usb_base);
    indexed_memory_set_index_step(64, 1);
    indexed_memory_set_index_flags(64, FLAG_AUTO_STEP);
    
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
    
    // User area (indexes 128-255) - point to user memory area
    for (int i = IDX_USER_START; i <= IDX_USER_END; i++) {
        uint32_t addr = MIA_USER_AREA_BASE + ((i - IDX_USER_START) * 1024); // 1KB per user index
        indexed_memory_set_index_address(i, addr);
        indexed_memory_set_index_default(i, addr);
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
    // Preserve flags in upper 8 bits
    uint8_t flags = (g_state.indexes[idx].current_addr >> 24) & 0xFF;
    g_state.indexes[idx].current_addr = (address & 0xFFFFFF) | (flags << 24);
}

/**
 * Set index default address
 */
void indexed_memory_set_index_default(uint8_t idx, uint32_t address) {
    g_state.indexes[idx].default_addr = address & 0xFFFFFF;
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
    // Store flags in upper 8 bits of current_addr
    uint32_t addr = g_state.indexes[idx].current_addr & 0xFFFFFF;
    g_state.indexes[idx].current_addr = addr | (flags << 24);
}

/**
 * Reset index to default address
 */
void indexed_memory_reset_index(uint8_t idx) {
    uint8_t flags = (g_state.indexes[idx].current_addr >> 24) & 0xFF;
    uint32_t default_addr = g_state.indexes[idx].default_addr & 0xFFFFFF;
    g_state.indexes[idx].current_addr = default_addr | (flags << 24);
}

/**
 * Read byte from index with auto-stepping
 */
uint8_t indexed_memory_read(uint8_t idx) {
    uint32_t addr = g_state.indexes[idx].current_addr & 0xFFFFFF;
    uint8_t flags = (g_state.indexes[idx].current_addr >> 24) & 0xFF;
    
    // Validate address
    if (!indexed_memory_is_valid_address(addr)) {
        indexed_memory_set_irq(IRQ_MEMORY_ERROR);
        g_state.status |= STATUS_MEMORY_ERROR;
        return 0xFF; // Return safe value
    }
    
    // Read data
    uint8_t data = mia_memory[addr - MIA_MEMORY_BASE];
    
    // Auto-step if enabled
    if (flags & FLAG_AUTO_STEP) {
        uint8_t step = g_state.indexes[idx].step;
        if (flags & FLAG_DIRECTION) {
            // Backward stepping
            if (addr >= step) {
                addr -= step;
            } else {
                indexed_memory_handle_overflow(idx);
                return data;
            }
        } else {
            // Forward stepping
            addr += step;
            if (addr >= MIA_MEMORY_BASE + MIA_MEMORY_SIZE) {
                indexed_memory_handle_overflow(idx);
                return data;
            }
        }
        
        // Update address with preserved flags
        g_state.indexes[idx].current_addr = addr | (flags << 24);
    }
    
    return data;
}

/**
 * Write byte to index with auto-stepping
 */
void indexed_memory_write(uint8_t idx, uint8_t data) {
    uint32_t addr = g_state.indexes[idx].current_addr & 0xFFFFFF;
    uint8_t flags = (g_state.indexes[idx].current_addr >> 24) & 0xFF;
    
    // Validate address
    if (!indexed_memory_is_valid_address(addr)) {
        indexed_memory_set_irq(IRQ_MEMORY_ERROR);
        g_state.status |= STATUS_MEMORY_ERROR;
        return;
    }
    
    // Write data
    mia_memory[addr - MIA_MEMORY_BASE] = data;
    
    // Auto-step if enabled
    if (flags & FLAG_AUTO_STEP) {
        uint8_t step = g_state.indexes[idx].step;
        if (flags & FLAG_DIRECTION) {
            // Backward stepping
            if (addr >= step) {
                addr -= step;
            } else {
                indexed_memory_handle_overflow(idx);
                return;
            }
        } else {
            // Forward stepping
            addr += step;
            if (addr >= MIA_MEMORY_BASE + MIA_MEMORY_SIZE) {
                indexed_memory_handle_overflow(idx);
                return;
            }
        }
        
        // Update address with preserved flags
        g_state.indexes[idx].current_addr = addr | (flags << 24);
    }
}

/**
 * Read byte from index without auto-stepping
 */
uint8_t indexed_memory_read_no_step(uint8_t idx) {
    uint32_t addr = g_state.indexes[idx].current_addr & 0xFFFFFF;
    
    // Validate address
    if (!indexed_memory_is_valid_address(addr)) {
        indexed_memory_set_irq(IRQ_MEMORY_ERROR);
        g_state.status |= STATUS_MEMORY_ERROR;
        return 0xFF;
    }
    
    return mia_memory[addr - MIA_MEMORY_BASE];
}

/**
 * Write byte to index without auto-stepping
 */
void indexed_memory_write_no_step(uint8_t idx, uint8_t data) {
    uint32_t addr = g_state.indexes[idx].current_addr & 0xFFFFFF;
    
    // Validate address
    if (!indexed_memory_is_valid_address(addr)) {
        indexed_memory_set_irq(IRQ_MEMORY_ERROR);
        g_state.status |= STATUS_MEMORY_ERROR;
        return;
    }
    
    mia_memory[addr - MIA_MEMORY_BASE] = data;
}

/**
 * Get configuration field value
 */
uint8_t indexed_memory_get_config_field(uint8_t idx, uint8_t field) {
    switch (field) {
        case CFG_ADDR_L:
            return (g_state.indexes[idx].current_addr) & 0xFF;
        case CFG_ADDR_M:
            return (g_state.indexes[idx].current_addr >> 8) & 0xFF;
        case CFG_ADDR_H:
            return (g_state.indexes[idx].current_addr >> 16) & 0xFF;
        case CFG_DEFAULT_L:
            return (g_state.indexes[idx].default_addr) & 0xFF;
        case CFG_DEFAULT_M:
            return (g_state.indexes[idx].default_addr >> 8) & 0xFF;
        case CFG_DEFAULT_H:
            return (g_state.indexes[idx].default_addr >> 16) & 0xFF;
        case CFG_STEP:
            return g_state.indexes[idx].step;
        case CFG_FLAGS:
            return (g_state.indexes[idx].current_addr >> 24) & 0xFF;
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
    uint32_t addr, flags;
    
    switch (field) {
        case CFG_ADDR_L:
            addr = g_state.indexes[idx].current_addr & 0xFFFF00FF;
            flags = g_state.indexes[idx].current_addr & 0xFF000000;
            g_state.indexes[idx].current_addr = addr | value | flags;
            break;
        case CFG_ADDR_M:
            addr = g_state.indexes[idx].current_addr & 0xFF00FFFF;
            flags = g_state.indexes[idx].current_addr & 0xFF000000;
            g_state.indexes[idx].current_addr = addr | (value << 8) | flags;
            break;
        case CFG_ADDR_H:
            addr = g_state.indexes[idx].current_addr & 0xFF0000FF;
            flags = g_state.indexes[idx].current_addr & 0xFF000000;
            g_state.indexes[idx].current_addr = addr | (value << 16) | flags;
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
        case CFG_STEP:
            g_state.indexes[idx].step = value;
            break;
        case CFG_FLAGS:
            addr = g_state.indexes[idx].current_addr & 0x00FFFFFF;
            g_state.indexes[idx].current_addr = addr | (value << 24);
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
 */
void indexed_memory_copy_byte(uint8_t src_idx, uint8_t dst_idx) {
    uint8_t data = indexed_memory_read(src_idx);
    indexed_memory_write(dst_idx, data);
}

/**
 * Copy block of bytes between indexes
 */
void indexed_memory_copy_block(uint8_t src_idx, uint8_t dst_idx, uint16_t count) {
    g_state.status |= STATUS_DMA_ACTIVE;
    
    for (uint16_t i = 0; i < count; i++) {
        uint8_t data = indexed_memory_read(src_idx);
        indexed_memory_write(dst_idx, data);
    }
    
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
 * Get IRQ cause
 */
uint8_t indexed_memory_get_irq_cause(void) {
    return g_state.irq_cause;
}

/**
 * Clear IRQ
 */
void indexed_memory_clear_irq(void) {
    g_state.status &= ~STATUS_IRQ_PENDING;
    g_state.irq_cause = IRQ_NO_IRQ;
}

/**
 * Set IRQ
 */
void indexed_memory_set_irq(uint8_t cause) {
    g_state.status |= STATUS_IRQ_PENDING;
    g_state.irq_cause = cause;
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

/**
 * Validate memory address
 */
bool indexed_memory_is_valid_address(uint32_t address) {
    return (address >= MIA_MEMORY_BASE && address < (MIA_MEMORY_BASE + MIA_MEMORY_SIZE));
}

/**
 * Handle address overflow/underflow
 */
void indexed_memory_handle_overflow(uint8_t idx) {
    g_state.status |= STATUS_INDEX_OVERFLOW;
    indexed_memory_set_irq(IRQ_INDEX_OVERFLOW);
    
    // Reset to default address on overflow
    indexed_memory_reset_index(idx);
}