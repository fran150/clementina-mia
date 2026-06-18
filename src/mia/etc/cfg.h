#ifndef _MIA_ETC_CFG_H_
#define _MIA_ETC_CFG_H_

#include <stdint.h>
#include "mem/indexes.h"
#include "mem/regs.h"
#include "sys/speed.h"

#define CFG_SPEED_L 0x20
#define CFG_SPEED_M 0x21
#define CFG_SPEED_H 0x22

// Resolves which index descriptor a config id targets. The high nibble bit
// selects window A or window B; the configured descriptor is whichever index
// that window currently has selected (idxa_selector / idxb_selector). This lets
// any of the 256 indexes be configured: bind it to a window, then use CFG.
static inline __force_inline uint8_t cfg_index_id(uint8_t id) {
    return ((id >> 4) & 0x01) ? mia_regs->idxb_selector
                              : mia_regs->idxa_selector;
}

// Gets the value of the specified config id
static inline __force_inline uint8_t get_cfg(uint8_t id) {
    if (id < 0x20) {
        uint8_t index_id = cfg_index_id(id);
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
    } else {
        switch (id) {
            case CFG_SPEED_L: return mia_speed_get_applied_byte(ADDR24_L);
            case CFG_SPEED_M: return mia_speed_get_applied_byte(ADDR24_M);
            case CFG_SPEED_H: return mia_speed_get_applied_byte(ADDR24_H);
        }
    }

    return 0;
}

// Sets the value corresponding to the specified config id
static inline __force_inline void set_cfg(uint8_t id, uint8_t value) {
    if (id < 0x20) {
        uint8_t index_id = cfg_index_id(id);
        uint8_t field = id & 0x0F;

        switch (field) {
            case 0x00: index_set_current_addr_byte(index_id, ADDR24_L, value); break;
            case 0x01: index_set_current_addr_byte(index_id, ADDR24_M, value); break;
            case 0x02: index_set_current_addr_byte(index_id, ADDR24_H, value); break;

            case 0x03: index_set_default_addr_byte(index_id, ADDR24_L, value); break;
            case 0x04: index_set_default_addr_byte(index_id, ADDR24_M, value); break;
            case 0x05: index_set_default_addr_byte(index_id, ADDR24_H, value); break;

            case 0x06: index_set_limit_addr_byte(index_id, ADDR24_L, value); break;
            case 0x07: index_set_limit_addr_byte(index_id, ADDR24_M, value); break;
            case 0x08: index_set_limit_addr_byte(index_id, ADDR24_H, value); break;

            case 0x09: index_set_step_byte(index_id, ADDR16_L, value); break;
            case 0x0A: index_set_step_byte(index_id, ADDR16_H, value); break;

            case 0x0B: index_set_flag(index_id, value); break;

            default: return;  // reserved field: nothing to update
        }

        // Only the current-address bytes (fields 0-2) change where the index
        // points, so only they need to refresh the window's data port. Skipping
        // the refresh for default/limit/step/flags writes keeps this bus-path
        // helper light. After repositioning, a read returns the new location's
        // byte without re-selecting the index.
        if (field < 3) {
            if ((id >> 4) & 0x01) {
                mia_regs->idxb_port = index_read(mia_regs->idxb_selector);
            } else {
                mia_regs->idxa_port = index_read(mia_regs->idxa_selector);
            }
        }

        return;
    }

    switch (id) {
        case CFG_SPEED_L:
            mia_speed_stage_byte(ADDR24_L, value);
            return;
        case CFG_SPEED_M:
            mia_speed_stage_byte(ADDR24_M, value);
            return;
        case CFG_SPEED_H:
            mia_speed_stage_byte(ADDR24_H, value);
            mia_speed_commit();
            return;
    }
}

#endif
