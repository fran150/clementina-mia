#ifndef _MIA_ETC_CFG_H_
#define _MIA_ETC_CFG_H_

#include <stdint.h>
#include "mem/indexes.h"

// Gets the value of the specified config id
static inline __force_inline uint8_t get_cfg(uint8_t id) {
    uint8_t index_id = (id >> 4) & 0x01;
    
    uint8_t field = id & 0x0F;

    switch (field) {
        case 0x00: return index_get_current_addr_byte(index_id, ADDR24_L);
        case 0x01: return index_get_current_addr_byte(index_id, ADDR24_M);
        case 0x02: return index_get_current_addr_byte(index_id, ADDR24_H);
        
        case 0x03: return index_get_default_addr_byte(index_id, ADDR24_L);
        case 0x04: return index_get_default_addr_byte(index_id, ADDR24_M);
        case 0x05: return index_get_default_addr_byte(index_id, ADDR24_H);
        
        case 0x06: return index_get_limit_addr_byte(index_id, ADDR24_L);
        case 0x07: return index_get_limit_addr_byte(index_id, ADDR24_M);
        case 0x08: return index_get_limit_addr_byte(index_id, ADDR24_H);
        
        case 0x09: return index_get_step_byte(index_id, ADDR16_L);
        case 0x0A: return index_get_step_byte(index_id, ADDR16_H);
        
        case 0x0B: return index_get_flag(index_id);        
    }

    return 0;
}

// Sets the value corresponding to the specified config id
static inline __force_inline void set_cfg(uint8_t id, uint8_t value) {
    uint8_t index_id = (id >> 4) & 0x01;
    
    uint8_t field = id & 0x0F;

    switch (field) {
        case 0x00: return index_set_current_addr_byte(index_id, ADDR24_L, value);
        case 0x01: return index_set_current_addr_byte(index_id, ADDR24_M, value);
        case 0x02: return index_set_current_addr_byte(index_id, ADDR24_H, value);
        
        case 0x03: return index_set_default_addr_byte(index_id, ADDR24_L, value);
        case 0x04: return index_set_default_addr_byte(index_id, ADDR24_M, value);
        case 0x05: return index_set_default_addr_byte(index_id, ADDR24_H, value);
        
        case 0x06: return index_set_limit_addr_byte(index_id, ADDR24_L, value);
        case 0x07: return index_set_limit_addr_byte(index_id, ADDR24_M, value);
        case 0x08: return index_set_limit_addr_byte(index_id, ADDR24_H, value);
        
        case 0x09: return index_set_step_byte(index_id, ADDR16_L, value);
        case 0x0A: return index_set_step_byte(index_id, ADDR16_H, value);
        
        case 0x0B: return index_set_flag(index_id, value);
    }
}

#endif