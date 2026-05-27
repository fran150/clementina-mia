#ifndef _MIA_REGS_H_
#define _MIA_REGS_H_

#include <stddef.h>
#include <stdbool.h>

#include "pico/stdlib.h"

// MIA Registers definition
typedef union {
    struct {
        uint8_t idxa_port;      // 00 (FFE0)
        uint8_t idxa_selector;  // 01 (FFE1)
        uint8_t cfg_selector;   // 02 (FFE2)
        uint8_t cfg_port;       // 03 (FFE3)
        uint8_t idxb_port;      // 04 (FFE4)
        uint8_t idxb_selector;  // 05 (FFE5)
        uint8_t cmd_param1;     // 06 (FFE6)
        uint8_t cmd_param2;     // 07 (FFE7)
        uint8_t cmd_param3;     // 08 (FFE8)
        uint8_t cmd_trigger;    // 09 (FFE9)
        uint16_t mia_status;    // 0A, 0B (FFEA, FFEB)
        uint16_t mia_error;     // 0C, 0D (FFEC, FFED)
        uint16_t irq_mask;      // 0E, 0F (FFEE, FFEF)
        uint16_t irq_status;    // 10, 11 (FFF0, FFF1)
        uint8_t reserved[8];    // 12 - 19 (FFF2 - FFF9)
        uint16_t nmi_vector;    // 1A, 1B (FFFA, FFFB)
        uint16_t reset_vector;  // 1C, 1D (FFFC, FFFD)
        uint16_t brk_vector;    // 1E, 1F (FFFE, FFFF)
    } __attribute__((packed));    
    uint8_t bytes[32];
} mia_regs_t;

// MIA registers
extern volatile mia_regs_t * const mia_regs;

// Convenience macro to access a given register with a 5 bit address
#define REGS(addr) mia_regs->bytes[(addr) & 0x1F]
// Convenience macro to access 2 registers and treat it like a 16 bit value
#define REGSW(addr) ((uint16_t *)&REGS(addr))[0]

#endif
