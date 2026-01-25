#ifndef _MEM_INDEXES_H_
#define _MEM_INDEXES_H_

#include <stdint.h>

#include "pico.h"

#include "mem/mem.h"

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

// 4kb index table
extern volatile index_t idx[256];

// Enums must be in the header so the caller knows the types
typedef enum { 
    ADDR24_L = 0, 
    ADDR24_M = 1, 
    ADDR24_H = 2 
} addr24_byte_t;

typedef enum { 
    ADDR16_L = 0, 
    ADDR16_H = 1 
} addr16_byte_t;

#define IDX_FLAG_R_STP_ENA          0
#define IDX_FLAG_W_STP_ENA          1
#define IDX_FLAG_STP_DIR            2
#define IDX_FLAG_WRAP_DIS           3
#define IDX_FLAG_WRAP_IRQ           4

static inline uint8_t __not_in_flash_func(read_index)(uint8_t index_id) {
    return mem[idx[index_id].current_addr];
}

static inline void __not_in_flash_func(write_index)(uint8_t index_id, uint8_t value) {
    mem[idx[index_id].current_addr] = value;
}

static inline uint8_t __not_in_flash_func(step_index_and_read)(uint8_t index_id) {
    volatile index_t *restrict entry = &idx[index_id];
    uint32_t flags = entry->flags;
    
    // Extract bits once using masks to allow the compiler to use bit-test instructions
    uint32_t is_enabled = (flags >> IDX_FLAG_R_STP_ENA) & 1;
    uint32_t is_forward = (flags >> IDX_FLAG_STP_DIR) & 1;
    
    // Convert 0/1 to -1/1 without a branch
    int32_t modifier = (is_forward << 1) - 1; 
    int32_t actual_step = (int32_t)(entry->step * is_enabled) * modifier;

    entry->current_addr += actual_step;

    if (entry->current_addr >= entry->limit_addr && !((flags >> IDX_FLAG_WRAP_DIS) & 1)) {
        entry->current_addr = entry->default_addr;
        // TODO: trigger IRQ
    }

    return mem[entry->current_addr];
}

static inline void __not_in_flash_func(write_index_and_step)(uint8_t index_id, uint8_t value) {
    volatile index_t *restrict entry = &idx[index_id];
    uint32_t flags = entry->flags;

    mem[entry->current_addr] = value;

    uint32_t is_enabled = (flags >> IDX_FLAG_W_STP_ENA) & 1;
    uint32_t is_forward = (flags >> IDX_FLAG_STP_DIR) & 1;

    int32_t modifier = (int32_t)(is_forward << 1) - 1;
    int32_t actual_step = (int32_t)(entry->step * is_enabled) * modifier;

    entry->current_addr += actual_step;
    
    if (entry->current_addr >= entry->limit_addr && !((flags >> IDX_FLAG_WRAP_DIS) & 1)) {
        entry->current_addr = entry->default_addr;
        // TODO: trigger IRQ
    }
}

static inline uint8_t __not_in_flash_func(get_index_current_address_byte)(uint8_t index_id, addr24_byte_t addr_byte) {
    uint8_t *ptr = (uint8_t *)&idx[index_id].current_addr;
    return ptr[addr_byte];
}

static inline void __not_in_flash_func(set_index_current_address_byte)(uint8_t index_id, addr24_byte_t addr_byte, uint8_t value) {
    uint8_t *ptr = (uint8_t *)&idx[index_id].current_addr;
    ptr[addr_byte] = value;
}


static inline uint8_t __not_in_flash_func(get_index_default_address_byte)(uint8_t index_id, addr24_byte_t addr_byte) {
    uint8_t *ptr = (uint8_t *)&idx[index_id].default_addr;
    return ptr[addr_byte];
}

static inline void __not_in_flash_func(set_index_default_address_byte)(uint8_t index_id, addr24_byte_t addr_byte, uint8_t value) {
    uint8_t *ptr = (uint8_t *)&idx[index_id].default_addr;
    ptr[addr_byte] = value;
}

static inline uint8_t __not_in_flash_func(get_index_limit_address_byte)(uint8_t index_id, addr24_byte_t addr_byte) {
    uint8_t *ptr = (uint8_t *)&idx[index_id].limit_addr;
    return ptr[addr_byte];
}

static inline void __not_in_flash_func(set_index_limit_address_byte)(uint8_t index_id, addr24_byte_t addr_byte, uint8_t value) {
    uint8_t *ptr = (uint8_t *)&idx[index_id].limit_addr;
    ptr[addr_byte] = value;
}

static inline uint8_t __not_in_flash_func(get_index_step_byte)(uint8_t index_id, addr16_byte_t addr_byte) {
    uint8_t *ptr = (uint8_t *)&idx[index_id].step;
    return ptr[addr_byte];
}

static inline void __not_in_flash_func(set_index_step_byte)(uint8_t index_id, addr16_byte_t addr_byte, uint8_t value) {
    uint8_t *ptr = (uint8_t *)&idx[index_id].step;
    ptr[addr_byte] = value;
}

static inline uint8_t __not_in_flash_func(get_index_flag)(uint8_t index_id) {
    return idx[index_id].flags;
}

static inline void __not_in_flash_func(set_index_flag)(uint8_t index_id, uint8_t value) {
    idx[index_id].flags = value;
}

#endif