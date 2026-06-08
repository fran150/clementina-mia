#ifndef _MIA_MEM_INDEXES_H_
#define _MIA_MEM_INDEXES_H_

#include <stdint.h>
#include <stdbool.h>

#include "pico.h"

#include "mem/mem.h"
#include "irq/irq.h"
#include "video/video_dirty.h"

// Memory index structure (16 bytes per index)
typedef union {
    struct {
        uint32_t current_addr; // Offset 0-3
        uint32_t default_addr; // Offset 4-7
        uint32_t limit_addr;   // Offset 8-11
        uint16_t step;         // Offset 12-13
        uint8_t  flags;        // Offset 14
        uint8_t  reserved;     // Offset 15 (Padding to make it 16 bytes)
    };
    uint8_t bytes[16];
} index_t;

#define MASK_24BIT 0x00FFFFFF

// 4kb index table with 256 indexes
extern volatile index_t idx[256];

// Parts of the 24 bit address
typedef enum { 
    ADDR24_L = 0,       // Low byte
    ADDR24_M = 1,       // Medium byte
    ADDR24_H = 2        // High byte
} addr24_byte_t;

// Parts of the 16 bit address
typedef enum { 
    ADDR16_L = 0,       // LSB
    ADDR16_H = 1        // MSB
} addr16_byte_t;

// MIA has 2 index windows
typedef enum {
    IDXA = 0,       // Index Window A
    IDXB = 1        // Index Window B
} index_win_t;

#define IDX_FLAG_R_STP_ENA          0       // Set if step on read is enabled
#define IDX_FLAG_W_STP_ENA          1       // Set if step on write is enabled
#define IDX_FLAG_STP_DIR            2       // Step direction: 0 is forward, 1 is backward
#define IDX_FLAG_WRAP_ENA           3       // Set if address wrap is enabled
#define IDX_FLAG_WRAP_IRQ           4       // Set if address wrap of this index triggers interrupt

/***************************************************************************************************
 * READ / WRITE from memory using index
 ***************************************************************************************************/

// Returns the value from the RAM memory to where the specified index is pointing to. 
// This command does not affect the index state
static inline __force_inline uint8_t __not_in_flash_func(index_read)(uint8_t index_id) {
    return mem[idx[index_id].current_addr & MIA_RAM_MASK];
}

// Writes the value to the RAM memory to where the specified index is pointing to. 
// This command does not affect the index state
static inline __force_inline void __not_in_flash_func(index_write)(uint8_t index_id, uint8_t value) {
    uint32_t offset = idx[index_id].current_addr & MIA_RAM_MASK;
    mem[offset] = value;
    mia_video_mark_dirty(offset);
}

// Steps the index according to it's configuration and reads the value in RAM to where the index ends up pointing to.
// Last value read should already be in the MIA register so this function is used to update to the new value after the
// processor has read the register
static inline __force_inline uint8_t __not_in_flash_func(index_step_and_read)(uint8_t index_id, index_win_t win) {
    volatile index_t *restrict entry = &idx[index_id];
    uint32_t flags = entry->flags;
    
    // Extract bits once using masks to allow the compiler to use bit-test instructions
    uint32_t is_enabled = (flags >> IDX_FLAG_R_STP_ENA) & 1;
    uint32_t is_backward = (flags >> IDX_FLAG_STP_DIR) & 1;
    
    // Convert 0/1 to 1/-1 without a branch.
    int32_t modifier = 1 - (int32_t)(is_backward << 1);
    int32_t actual_step = (int32_t)(entry->step * is_enabled) * modifier;

    entry->current_addr += actual_step;

    if ((flags >> IDX_FLAG_WRAP_ENA) & 1) {
        bool wrapped = false;
        if (is_backward) {
            if (entry->current_addr < entry->default_addr) {
                entry->current_addr = entry->limit_addr - 1;
                wrapped = true;
            }
        } else if (entry->current_addr >= entry->limit_addr) {
            entry->current_addr = entry->default_addr;
            wrapped = true;
        }

        if (wrapped && ((flags >> IDX_FLAG_WRAP_IRQ) & 1)) {
            mia_irq_set_flag(win == IDXA ? IRQ_IDXA_WRAPPED : IRQ_IDXB_WRAPPED);
        }
    }

    return mem[entry->current_addr & MIA_RAM_MASK];
}

// Writes the value in RAM to where the index is pointing to and steps the index according to it's configuration.
// The processor should have written the value to the register so this function is used to move the value to the RAM.
static inline __force_inline void __not_in_flash_func(index_write_and_step)(uint8_t index_id, uint8_t value, index_win_t win) {
    volatile index_t *restrict entry = &idx[index_id];
    uint32_t flags = entry->flags;
    uint32_t write_offset = entry->current_addr & MIA_RAM_MASK;

    mem[write_offset] = value;
    mia_video_mark_dirty(write_offset);

    uint32_t is_enabled = (flags >> IDX_FLAG_W_STP_ENA) & 1;
    uint32_t is_backward = (flags >> IDX_FLAG_STP_DIR) & 1;

    int32_t modifier = 1 - (int32_t)(is_backward << 1);
    int32_t actual_step = (int32_t)(entry->step * is_enabled) * modifier;

    entry->current_addr += actual_step;
    
    if ((flags >> IDX_FLAG_WRAP_ENA) & 1) {
        bool wrapped = false;
        if (is_backward) {
            if (entry->current_addr < entry->default_addr) {
                entry->current_addr = entry->limit_addr - 1;
                wrapped = true;
            }
        } else if (entry->current_addr >= entry->limit_addr) {
            entry->current_addr = entry->default_addr;
            wrapped = true;
        }

        if (wrapped && ((flags >> IDX_FLAG_WRAP_IRQ) & 1)) {
            mia_irq_set_flag(win == IDXA ? IRQ_IDXA_WRAPPED : IRQ_IDXB_WRAPPED);
        }
    }
}

// Resets the specified index. This moves the current address to the default address
static inline __force_inline void __not_in_flash_func(reset_index)(uint8_t index_id) {
    volatile index_t *restrict entry = &idx[index_id];
    entry->current_addr = entry->default_addr; 
}

/***************************************************************************************************
 * GET / SET current address
 ***************************************************************************************************/

// Gets the specified byte of the current address
static inline __force_inline uint8_t __not_in_flash_func(index_get_current_addr_byte)(uint8_t index_id, addr24_byte_t addr_byte) {
    uint8_t *ptr = (uint8_t *)&idx[index_id].current_addr;
    return ptr[addr_byte];
}

// Sets the specified byte of the current address
static inline __force_inline void __not_in_flash_func(index_set_current_addr_byte)(uint8_t index_id, addr24_byte_t addr_byte, uint8_t value) {
    uint8_t *ptr = (uint8_t *)&idx[index_id].current_addr;
    ptr[addr_byte] = value;
}

// Gets the specified the current address (full 24 bits)
static inline __force_inline uint32_t __not_in_flash_func(index_get_current_addr)(uint8_t index_id) {
    return idx[index_id].current_addr & MASK_24BIT;
}

// Sets the specified value as the current address (full 24 bits)
static inline __force_inline void __not_in_flash_func(index_set_current_addr)(uint8_t index_id, uint32_t value) {
    idx[index_id].current_addr = value & MASK_24BIT;
}


/***************************************************************************************************
 * GET / SET default address
 ***************************************************************************************************/

// Gets the specified byte of the default address
static inline __force_inline uint8_t __not_in_flash_func(index_get_default_addr_byte)(uint8_t index_id, addr24_byte_t addr_byte) {
    uint8_t *ptr = (uint8_t *)&idx[index_id].default_addr;
    return ptr[addr_byte];
}

// Sets the specified byte of the default address
static inline __force_inline void __not_in_flash_func(index_set_default_addr_byte)(uint8_t index_id, addr24_byte_t addr_byte, uint8_t value) {
    uint8_t *ptr = (uint8_t *)&idx[index_id].default_addr;
    ptr[addr_byte] = value;
}

// Gets the specified the default address (full 24 bits)
static inline __force_inline uint32_t __not_in_flash_func(index_get_default_addr)(uint8_t index_id) {
    return idx[index_id].default_addr & MASK_24BIT;
}

// Sets the specified value as the default address (full 24 bits)
static inline __force_inline void __not_in_flash_func(index_set_default_addr)(uint8_t index_id, uint32_t value) {
    idx[index_id].default_addr = value & MASK_24BIT;
}

/***************************************************************************************************
 * GET / SET limit address
 ***************************************************************************************************/

// Gets the specified byte of the limit address
static inline __force_inline uint8_t __not_in_flash_func(index_get_limit_addr_byte)(uint8_t index_id, addr24_byte_t addr_byte) {
    uint8_t *ptr = (uint8_t *)&idx[index_id].limit_addr;
    return ptr[addr_byte];
}

// Sets the specified byte of the limit address
static inline __force_inline void __not_in_flash_func(index_set_limit_addr_byte)(uint8_t index_id, addr24_byte_t addr_byte, uint8_t value) {
    uint8_t *ptr = (uint8_t *)&idx[index_id].limit_addr;
    ptr[addr_byte] = value;
}

// Gets the specified the limit address (full 24 bits)
static inline __force_inline uint32_t __not_in_flash_func(index_get_limit_addr)(uint8_t index_id) {
    return idx[index_id].limit_addr & MASK_24BIT;
}

// Sets the specified value as the limit address (full 24 bits)
static inline __force_inline void __not_in_flash_func(index_set_limit_addr)(uint8_t index_id, uint32_t value) {
    idx[index_id].limit_addr = value & MASK_24BIT;
}


/***************************************************************************************************
 * GET / SET step
 ***************************************************************************************************/

// Get the specified index step size
static inline __force_inline uint8_t __not_in_flash_func(index_get_step_byte)(uint8_t index_id, addr16_byte_t addr_byte) {
    uint8_t *ptr = (uint8_t *)&idx[index_id].step;
    return ptr[addr_byte];
}

// Sets the step size of the specified index
static inline __force_inline void __not_in_flash_func(index_set_step_byte)(uint8_t index_id, addr16_byte_t addr_byte, uint8_t value) {
    uint8_t *ptr = (uint8_t *)&idx[index_id].step;
    ptr[addr_byte] = value;
}


/***************************************************************************************************
 * GET / SET flag
 ***************************************************************************************************/

 // Get the specified index configuration flag 
static inline __force_inline uint8_t __not_in_flash_func(index_get_flag)(uint8_t index_id) {
    return idx[index_id].flags;
}

// Sets the configuration flag of the specified index
static inline __force_inline void __not_in_flash_func(index_set_flag)(uint8_t index_id, uint8_t value) {
    idx[index_id].flags = value;
}

#endif
