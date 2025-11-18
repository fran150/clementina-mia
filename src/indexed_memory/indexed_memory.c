/**
 * MIA Indexed Memory System Implementation
 * 
 * Core implementation of the 256-index memory system with automatic stepping,
 * configuration management, and DMA capabilities.
 */

#include "indexed_memory.h"
#include "indexed_memory_dma.h"
#include "hardware/gpio_mapping.h"
#include "pico/util/queue.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// Always include GPIO for IRQ line control (mocked in tests)
#include "hardware/gpio.h"
#include "hardware/watchdog.h"

// Inter-core command queue
#define COMMAND_QUEUE_SIZE 8
static queue_t command_queue;

// Forward declarations
static void indexed_memory_reset_index(uint8_t idx);
static void indexed_memory_copy_block(uint8_t src_idx, uint8_t dst_idx, uint16_t count);
static void indexed_memory_set_address(uint8_t idx, addr_field_t field, uint32_t address);

// MIA memory layout (256KB properly allocated)
// All addresses are logical offsets (0x000000 - 0x03FFFF) into the mia_memory array
#define MIA_MEMORY_BASE         0x00000000  // Logical base (offset 0)
#define MIA_INDEX_TABLE_BASE    0x00000000  // 2KB
#define MIA_SYSTEM_AREA_BASE    0x00000800  // 16KB
#define MIA_VIDEO_AREA_BASE     0x00004800  // 60KB
#define MIA_USER_AREA_BASE      0x00013800  // 162KB
#define MIA_IO_BUFFER_BASE      0x0003C000  // 16KB
#define MIA_MEMORY_SIZE         0x00040000  // 256KB total MIA memory

// Global system state
static indexed_memory_state_t g_state;

// MIA memory array - properly allocated by linker to avoid SDK conflicts
// All index addresses are offsets into this array
static uint8_t mia_memory[MIA_MEMORY_SIZE] __attribute__((aligned(4)));

/**
 * DMA completion callback - called when DMA transfer completes
 */
static void dma_completion_callback(void) {
    // Clear DMA active status
    g_state.status &= ~STATUS_DMA_ACTIVE;
    
    // Signal completion
    irq_set_bits(IRQ_DMA_COMPLETE);
}

/**
 * Initialize the indexed memory system
 */
void indexed_memory_init(void) {    
    // Clear all state
    memset(&g_state, 0, sizeof(g_state));
    
    // Clear MIA memory
    memset(mia_memory, 0, MIA_MEMORY_SIZE);
    
    // Initialize system status
    g_state.status = STATUS_SYSTEM_READY;
    
    // Pre-configure system indexes
    
    // Index 0: System error log
    indexed_memory_set_address(IDX_SYSTEM_ERROR, ADDR_CURRENT, MIA_SYSTEM_AREA_BASE);
    indexed_memory_set_address(IDX_SYSTEM_ERROR, ADDR_DEFAULT, MIA_SYSTEM_AREA_BASE);
    g_state.indexes[IDX_SYSTEM_ERROR].step = 1;
    g_state.indexes[IDX_SYSTEM_ERROR].flags = FLAG_AUTO_STEP;
    
    // Character tables (indexes 16-23) - 8 tables, shared by background and sprites
    for (int i = 0; i < 8; i++) {
        uint8_t idx = IDX_CHARACTER_START + i;
        uint32_t addr = MIA_VIDEO_AREA_BASE + (i * 256 * 24); // 256 chars × 24 bytes each
        indexed_memory_set_address(idx, ADDR_CURRENT, addr);
        indexed_memory_set_address(idx, ADDR_DEFAULT, addr);
        indexed_memory_set_address(idx, ADDR_LIMIT, addr + (256 * 24)); // Wrap at end of character table (6KB)
        g_state.indexes[idx].step = 1;
        g_state.indexes[idx].flags = FLAG_AUTO_STEP | FLAG_WRAP_ON_LIMIT;
    }
    
    // Palette banks (indexes 32-47) - 16 banks, shared resource
    uint32_t palette_base = MIA_VIDEO_AREA_BASE + (8 * 256 * 24); // After 8 char tables (48KB)
    for (int i = 0; i < 16; i++) {
        uint8_t idx = IDX_PALETTE_START + i;
        uint32_t addr = palette_base + (i * 16); // 8 colors × 2 bytes per bank
        indexed_memory_set_address(idx, ADDR_CURRENT, addr);
        indexed_memory_set_address(idx, ADDR_DEFAULT, addr);
        indexed_memory_set_address(idx, ADDR_LIMIT, addr + 16); // Wrap at end of palette bank (16 bytes)
        g_state.indexes[idx].step = 1;
        g_state.indexes[idx].flags = FLAG_AUTO_STEP | FLAG_WRAP_ON_LIMIT;
    }
    
    // Nametables (indexes 48-51) - 4 tables for double buffering and scrolling
    uint32_t nametable_base = palette_base + (16 * 16); // After palette banks (256 bytes)
    for (int i = 0; i < 4; i++) {
        uint8_t idx = IDX_NAMETABLE_START + i;
        uint32_t addr = nametable_base + (i * 40 * 25); // 40×25 bytes per nametable
        indexed_memory_set_address(idx, ADDR_CURRENT, addr);
        indexed_memory_set_address(idx, ADDR_DEFAULT, addr);
        indexed_memory_set_address(idx, ADDR_LIMIT, addr + (40 * 25)); // Wrap at end of nametable (1000 bytes)
        g_state.indexes[idx].step = 1;
        g_state.indexes[idx].flags = FLAG_AUTO_STEP | FLAG_WRAP_ON_LIMIT;
    }
    
    // Palette tables (indexes 52-55) - 4 tables for double buffering and scrolling
    uint32_t palette_table_base = nametable_base + (4 * 40 * 25); // After nametables (4KB)
    for (int i = 0; i < 4; i++) {
        uint8_t idx = IDX_PALETTE_TABLE_START + i;
        uint32_t addr = palette_table_base + (i * 40 * 25); // 40×25 bytes per palette table
        indexed_memory_set_address(idx, ADDR_CURRENT, addr);
        indexed_memory_set_address(idx, ADDR_DEFAULT, addr);
        indexed_memory_set_address(idx, ADDR_LIMIT, addr + (40 * 25)); // Wrap at end of palette table (1000 bytes)
        g_state.indexes[idx].step = 1;
        g_state.indexes[idx].flags = FLAG_AUTO_STEP | FLAG_WRAP_ON_LIMIT;
    }
    
    // Sprite OAM (index 56) - 256 sprites × 4 bytes, sprites use character table graphics
    uint32_t sprite_oam_base = palette_table_base + (4 * 40 * 25); // After palette tables (4KB)
    indexed_memory_set_address(IDX_SPRITE_OAM, ADDR_CURRENT, sprite_oam_base);
    indexed_memory_set_address(IDX_SPRITE_OAM, ADDR_DEFAULT, sprite_oam_base);
    indexed_memory_set_address(IDX_SPRITE_OAM, ADDR_LIMIT, sprite_oam_base + (256 * 4)); // Wrap at end of OAM (1024 bytes)
    g_state.indexes[IDX_SPRITE_OAM].step = 4; // Step by sprite record size (4 bytes)
    g_state.indexes[IDX_SPRITE_OAM].flags = FLAG_AUTO_STEP | FLAG_WRAP_ON_LIMIT;
    
    // Active frame control (index 57) - selects which buffer set (0 or 1) for video transmission
    uint32_t active_frame_base = sprite_oam_base + (256 * 4); // After sprite OAM (1KB)
    indexed_memory_set_address(IDX_ACTIVE_FRAME, ADDR_CURRENT, active_frame_base);
    indexed_memory_set_address(IDX_ACTIVE_FRAME, ADDR_DEFAULT, active_frame_base);
    g_state.indexes[IDX_ACTIVE_FRAME].step = 1;
    g_state.indexes[IDX_ACTIVE_FRAME].flags = 0; // No auto-step
    
    // USB keyboard buffer (indexes 64-79)
    uint32_t usb_base = MIA_IO_BUFFER_BASE;
    
    // Index 64: USB keyboard circular buffer
    indexed_memory_set_address(64, ADDR_CURRENT, usb_base);
    indexed_memory_set_address(64, ADDR_DEFAULT, usb_base);
    indexed_memory_set_address(64, ADDR_LIMIT, usb_base + 64); // Wrap at end of keyboard buffer (64 bytes)
    g_state.indexes[64].step = 1;
    g_state.indexes[64].flags = FLAG_AUTO_STEP | FLAG_WRAP_ON_LIMIT;
    
    // Index 65: USB status
    indexed_memory_set_address(65, ADDR_CURRENT, usb_base + 64);
    indexed_memory_set_address(65, ADDR_DEFAULT, usb_base + 64);
    g_state.indexes[65].step = 1;
    g_state.indexes[65].flags = 0; // No auto-step for status
    
    // System control (indexes 80-95)
    uint32_t sysctrl_base = MIA_SYSTEM_AREA_BASE + 0x1000;
    
    // Index 80: Clock control
    indexed_memory_set_address(80, ADDR_CURRENT, sysctrl_base);
    indexed_memory_set_address(80, ADDR_DEFAULT, sysctrl_base);
    g_state.indexes[80].step = 1;
    g_state.indexes[80].flags = 0;
    
    // Index 81: Reset control
    indexed_memory_set_address(81, ADDR_CURRENT, sysctrl_base + 16);
    indexed_memory_set_address(81, ADDR_DEFAULT, sysctrl_base + 16);
    g_state.indexes[81].step = 1;
    g_state.indexes[81].flags = 0;
    
    // Index 83: IRQ mask control low byte (enable/disable interrupt sources 0-7)
    indexed_memory_set_address(83, ADDR_CURRENT, sysctrl_base + 48);
    indexed_memory_set_address(83, ADDR_DEFAULT, sysctrl_base + 48);
    g_state.indexes[83].step = 1;
    g_state.indexes[83].flags = 0;
    
    // Index 84: IRQ mask control high byte (enable/disable interrupt sources 8-15)
    indexed_memory_set_address(84, ADDR_CURRENT, sysctrl_base + 49);
    indexed_memory_set_address(84, ADDR_DEFAULT, sysctrl_base + 49);
    g_state.indexes[84].step = 1;
    g_state.indexes[84].flags = 0;
    
    // User area (indexes 128-255) - 162KB of user RAM
    // All user indexes start at the base of user memory
    // Users will reconfigure as needed for their applications
    for (int i = IDX_USER_START; i <= IDX_USER_END; i++) {
        indexed_memory_set_address(i, ADDR_CURRENT, MIA_USER_AREA_BASE);
        indexed_memory_set_address(i, ADDR_DEFAULT, MIA_USER_AREA_BASE);
        g_state.indexes[i].step = 1;
        g_state.indexes[i].flags = FLAG_AUTO_STEP;
    }
    
    // Initialize DMA for memory copy operations
    int dma_channel = indexed_memory_dma_init();
    indexed_memory_dma_set_completion_callback(dma_completion_callback);
    
    // Initialize inter-core command queue
    queue_init(&command_queue, sizeof(copy_command_t), COMMAND_QUEUE_SIZE);
    
    printf("Indexed memory system initialized with 256 indexes\n");
    printf("DMA channel %d claimed for memory operations\n", dma_channel);
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
 * Generic address setter - replaces three separate functions
 */
static void indexed_memory_set_address(uint8_t idx, addr_field_t field, uint32_t address) {
    address &= 0xFFFFFF;  // Ensure 24-bit
    switch (field) {
        case ADDR_CURRENT: g_state.indexes[idx].current_addr = address; break;
        case ADDR_DEFAULT: g_state.indexes[idx].default_addr = address; break;
        case ADDR_LIMIT:   g_state.indexes[idx].limit_addr = address; break;
    }
}



/**
 * Reset index to default address
 */
void indexed_memory_reset_index(uint8_t idx) {
    g_state.indexes[idx].current_addr = g_state.indexes[idx].default_addr;
}

/**
 * Read byte from index with auto-stepping - optimized critical path
 */
uint8_t indexed_memory_read(uint8_t idx) {
    index_t *index = &g_state.indexes[idx];
    uint32_t addr = index->current_addr;
    
    // Fast address validation
    CHECK_ADDR_OR_RETURN(addr, 0);
    
    // Read data
    uint8_t data = mia_memory[addr];
    
    // Auto-step if enabled - optimized path
    if (index->flags & FLAG_AUTO_STEP) {
        uint8_t step = index->step;
        
        if (index->flags & FLAG_DIRECTION) {
            // Backward stepping
            addr -= step;
            if ((index->flags & FLAG_WRAP_ON_LIMIT) && addr < index->limit_addr) {
                addr = index->default_addr;
            }
        } else {
            // Forward stepping
            addr += step;
            if ((index->flags & FLAG_WRAP_ON_LIMIT) && addr >= index->limit_addr) {
                addr = index->default_addr;
            }
        }
        
        // Update address
        index->current_addr = addr;
    }
    
    return data;
}

/**
 * Write byte to index with auto-stepping
 */
void indexed_memory_write(uint8_t idx, uint8_t data) {
    index_t *index = &g_state.indexes[idx];
    uint32_t addr = index->current_addr;
    
    // Fast address validation
    CHECK_ADDR_OR_RETURN_VOID(addr);
    
    // Write data
    mia_memory[addr] = data;
    
    // Auto-step if enabled - same logic as read
    if (index->flags & FLAG_AUTO_STEP) {
        uint8_t step = index->step;
        
        if (index->flags & FLAG_DIRECTION) {
            addr -= step;
            if ((index->flags & FLAG_WRAP_ON_LIMIT) && addr < index->limit_addr) {
                addr = index->default_addr;
            }
        } else {
            addr += step;
            if ((index->flags & FLAG_WRAP_ON_LIMIT) && addr >= index->limit_addr) {
                addr = index->default_addr;
            }
        }
        
        index->current_addr = addr;
    }
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
 * Execute window-level command
 * These commands operate on a specific index (typically the active index for a window)
 */
void indexed_memory_execute_window_command(uint8_t idx, uint8_t cmd) {
    switch (cmd) {
        case CMD_NOP:
            // No operation
            break;
        case CMD_RESET_INDEX:
            // Reset current address to default address
            g_state.indexes[idx].current_addr = g_state.indexes[idx].default_addr;
            break;
        case CMD_SET_DEFAULT_TO_ADDR:
            // Set default address to current address
            g_state.indexes[idx].default_addr = g_state.indexes[idx].current_addr;
            break;
        case CMD_SET_LIMIT_TO_ADDR:
            // Set limit address to current address
            g_state.indexes[idx].limit_addr = g_state.indexes[idx].current_addr;
            break;
        default:
            // Unknown window command - ignore
            break;
    }
}

/**
 * Execute shared/system-level command
 * These commands affect the entire system, not a specific index
 */
void indexed_memory_execute_shared_command(uint8_t cmd) {
    switch (cmd) {
        case CMD_SHARED_NOP:
            // No operation
            break;
        case CMD_RESET_ALL_IDX:
            // Reset all 256 indexes to their default addresses
            indexed_memory_reset_all();
            break;
        case CMD_FACTORY_RESET_ALL_IDX:
            // Factory reset: reinitialize the indexed memory subsystem
            // This resets all indexes to factory defaults, clears IRQ state,
            // resets DMA config, and clears all MIA memory
            // Does NOT reset other MIA components (ROM emulator, clock, etc.)
            irq_clear_all();
            indexed_memory_init();
            break;
        case CMD_CLEAR_IRQ:
            // Clear all pending interrupts
            irq_clear_all();
            break;
        case CMD_COPY_BLOCK:
            // Enqueue copy command for Core 1 processing
            {
                copy_command_t cmd = {
                    .src_idx = g_state.dma_config.src_idx,
                    .dst_idx = g_state.dma_config.dst_idx,
                    .count = g_state.dma_config.count
                };
                queue_try_add(&command_queue, &cmd);
            }
            break;
        case CMD_SYSTEM_RESET:
            // Full system reset: reboot the Pico via watchdog
            // This triggers a complete hardware reset equivalent to power cycling
            // During reboot, the normal initialization sequence will:
            // 1. Assert the 6502 reset line
            // 2. Initialize all systems
            // 3. Release the 6502 reset line to start boot sequence
            watchdog_reboot(0, 0, 0);
            
            // On real hardware: execution never reaches here (system reboots)
            // In tests: mock function returns and we continue normally
            break;
        default:
            // Unknown shared command - ignore
            break;
    }
}



/**
 * Copy block of bytes between indexes using DMA
 * This is asynchronous - the function returns immediately and DMA runs in background
 * IRQ_DMA_COMPLETE will be triggered when transfer completes
 */
void indexed_memory_copy_block(uint8_t src_idx, uint8_t dst_idx, uint16_t count) {
    if (count == 0) {
        return;
    }
    
    uint32_t src_addr = g_state.indexes[src_idx].current_addr;
    uint32_t dst_addr = g_state.indexes[dst_idx].current_addr;
    
    // Validate addresses
    if (src_addr >= MIA_MEMORY_SIZE || dst_addr >= MIA_MEMORY_SIZE) {
        g_state.status |= STATUS_MEMORY_ERROR;
        irq_set_bits(IRQ_MEMORY_ERROR);
        return;
    }
    
    // Check if transfer would exceed memory bounds
    if (src_addr + count > MIA_MEMORY_SIZE || dst_addr + count > MIA_MEMORY_SIZE) {
        g_state.status |= STATUS_MEMORY_ERROR;
        irq_set_bits(IRQ_DMA_ERROR);
        return;
    }
    
    // Check if DMA is already active
    if (g_state.status & STATUS_DMA_ACTIVE) {
        // DMA already in progress - reject new transfer to avoid blocking
        // This prevents the MIA from missing 6502 bus timing requirements
        // The 6502 can check STATUS_DMA_ACTIVE before initiating transfers
        // or wait for IRQ_DMA_COMPLETE interrupt
        irq_set_bits(IRQ_DMA_ERROR);
        return;
    }
    
    // Set DMA active status
    g_state.status |= STATUS_DMA_ACTIVE;
    
    // Start DMA transfer (asynchronous)
    // Note: Indexes are used only as address pointers and are NOT modified
    // The source and destination indexes remain unchanged after the copy
    indexed_memory_dma_start_transfer(
        &mia_memory[dst_addr],
        &mia_memory[src_addr],
        count
    );
    
    // Note: Function returns immediately, DMA continues in background
    // IRQ_DMA_COMPLETE will be triggered when transfer finishes
    // Indexes are NOT modified - they still point to the original addresses
}

/**
 * Set status bits (OR operation)
 * Sets the specified status bit(s) in the status register
 */
void indexed_memory_set_status(uint8_t status_bits) {
    g_state.status |= status_bits;
}

/**
 * Clear status bits (AND NOT operation)
 * Clears the specified status bit(s) from the status register
 */
void indexed_memory_clear_status(uint8_t status_bits) {
    g_state.status &= ~status_bits;
}

/**
 * Get status register value
 */
uint8_t indexed_memory_get_status(void) {
    return g_state.status;
}

/**
 * Process copy commands from Core 0 (called from Core 1)
 */
void indexed_memory_process_copy_command(void) {
    copy_command_t cmd;
    if (queue_try_remove(&command_queue, &cmd)) {
        indexed_memory_copy_block(cmd.src_idx, cmd.dst_idx, cmd.count);
    }
}