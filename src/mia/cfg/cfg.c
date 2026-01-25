#include "cfg.h" 

#include "mem/indexes.h"

uint8_t __not_in_flash_func(get_cfg)(uint8_t id) {
    uint8_t index_id = (id >> 4) & 0x01;
    
    uint8_t field = id & 0x0F;

    switch (field) {
        case 0x00: return get_index_current_address_byte(index_id, ADDR24_L);
        case 0x01: return get_index_current_address_byte(index_id, ADDR24_M);
        case 0x02: return get_index_current_address_byte(index_id, ADDR24_H);
        
        case 0x03: return get_index_default_address_byte(index_id, ADDR24_L);
        case 0x04: return get_index_default_address_byte(index_id, ADDR24_M);
        case 0x05: return get_index_default_address_byte(index_id, ADDR24_H);
        
        case 0x06: return get_index_limit_address_byte(index_id, ADDR24_L);
        case 0x07: return get_index_limit_address_byte(index_id, ADDR24_M);
        case 0x08: return get_index_limit_address_byte(index_id, ADDR24_H);
        
        case 0x09: return get_index_step_byte(index_id, ADDR16_L);
        case 0x0A: return get_index_step_byte(index_id, ADDR16_H);
        
        case 0x0B: return get_index_flag(index_id);        
    }

    return 0;
}

void __not_in_flash_func(set_cfg)(uint8_t id, uint8_t value) {
    uint8_t index_id = (id >> 4) & 0x01;
    
    uint8_t field = id & 0x0F;

    switch (field) {
        case 0x00: return set_index_current_address_byte(index_id, ADDR24_L, value);
        case 0x01: return set_index_current_address_byte(index_id, ADDR24_M, value);
        case 0x02: return set_index_current_address_byte(index_id, ADDR24_H, value);
        
        case 0x03: return set_index_default_address_byte(index_id, ADDR24_L, value);
        case 0x04: return set_index_default_address_byte(index_id, ADDR24_M, value);
        case 0x05: return set_index_default_address_byte(index_id, ADDR24_H, value);
        
        case 0x06: return set_index_limit_address_byte(index_id, ADDR24_L, value);
        case 0x07: return set_index_limit_address_byte(index_id, ADDR24_M, value);
        case 0x08: return set_index_limit_address_byte(index_id, ADDR24_H, value);
        
        case 0x09: return set_index_step_byte(index_id, ADDR16_L, value);
        case 0x0A: return set_index_step_byte(index_id, ADDR16_H, value);
        
        case 0x0B: return set_index_flag(index_id, value);
    }
}