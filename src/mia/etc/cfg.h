#ifndef _MIA_ETC_CFG_H_
#define _MIA_ETC_CFG_H_

#include <stdint.h>
#include "mem/indexes.h"
#include "mem/regs.h"
#include "sys/speed.h"

// Config ids (CFG_SELECT values). README.md "Configuration Registers" lists them
// without the CFG_ prefix. Ids $00-$0B configure the index selected in window A
// and $10-$1B the one in window B; the low nibble is the field, so the window A
// ids double as field numbers. $0C-$0F and $1C-$1F are reserved.
#define CFG_IDXA_ADDR_L 0x00u
#define CFG_IDXA_ADDR_M 0x01u
#define CFG_IDXA_ADDR_H 0x02u
#define CFG_IDXA_DEF_L 0x03u
#define CFG_IDXA_DEF_M 0x04u
#define CFG_IDXA_DEF_H 0x05u
#define CFG_IDXA_LIM_L 0x06u
#define CFG_IDXA_LIM_M 0x07u
#define CFG_IDXA_LIM_H 0x08u
#define CFG_IDXA_STP_L 0x09u
#define CFG_IDXA_STP_H 0x0Au
#define CFG_IDXA_FLAGS 0x0Bu

#define CFG_IDXB_ADDR_L 0x10u
#define CFG_IDXB_ADDR_M 0x11u
#define CFG_IDXB_ADDR_H 0x12u
#define CFG_IDXB_DEF_L 0x13u
#define CFG_IDXB_DEF_M 0x14u
#define CFG_IDXB_DEF_H 0x15u
#define CFG_IDXB_LIM_L 0x16u
#define CFG_IDXB_LIM_M 0x17u
#define CFG_IDXB_LIM_H 0x18u
#define CFG_IDXB_STP_L 0x19u
#define CFG_IDXB_STP_H 0x1Au
#define CFG_IDXB_FLAGS 0x1Bu

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
            case CFG_IDXA_ADDR_L: return index_get_current_addr_byte(index_id, ADDR24_L);
            case CFG_IDXA_ADDR_M: return index_get_current_addr_byte(index_id, ADDR24_M);
            case CFG_IDXA_ADDR_H: return index_get_current_addr_byte(index_id, ADDR24_H);
            
            case CFG_IDXA_DEF_L: return index_get_default_addr_byte(index_id, ADDR24_L);
            case CFG_IDXA_DEF_M: return index_get_default_addr_byte(index_id, ADDR24_M);
            case CFG_IDXA_DEF_H: return index_get_default_addr_byte(index_id, ADDR24_H);
            
            case CFG_IDXA_LIM_L: return index_get_limit_addr_byte(index_id, ADDR24_L);
            case CFG_IDXA_LIM_M: return index_get_limit_addr_byte(index_id, ADDR24_M);
            case CFG_IDXA_LIM_H: return index_get_limit_addr_byte(index_id, ADDR24_H);
            
            case CFG_IDXA_STP_L: return index_get_step_byte(index_id, ADDR16_L);
            case CFG_IDXA_STP_H: return index_get_step_byte(index_id, ADDR16_H);
            
            case CFG_IDXA_FLAGS: return index_get_flag(index_id);        
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
            case CFG_IDXA_ADDR_L: index_set_current_addr_byte(index_id, ADDR24_L, value); break;
            case CFG_IDXA_ADDR_M: index_set_current_addr_byte(index_id, ADDR24_M, value); break;
            case CFG_IDXA_ADDR_H: index_set_current_addr_byte(index_id, ADDR24_H, value); break;

            case CFG_IDXA_DEF_L: index_set_default_addr_byte(index_id, ADDR24_L, value); break;
            case CFG_IDXA_DEF_M: index_set_default_addr_byte(index_id, ADDR24_M, value); break;
            case CFG_IDXA_DEF_H: index_set_default_addr_byte(index_id, ADDR24_H, value); break;

            case CFG_IDXA_LIM_L: index_set_limit_addr_byte(index_id, ADDR24_L, value); break;
            case CFG_IDXA_LIM_M: index_set_limit_addr_byte(index_id, ADDR24_M, value); break;
            case CFG_IDXA_LIM_H: index_set_limit_addr_byte(index_id, ADDR24_H, value); break;

            case CFG_IDXA_STP_L: index_set_step_byte(index_id, ADDR16_L, value); break;
            case CFG_IDXA_STP_H: index_set_step_byte(index_id, ADDR16_H, value); break;

            case CFG_IDXA_FLAGS: index_set_flag(index_id, value); break;

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
