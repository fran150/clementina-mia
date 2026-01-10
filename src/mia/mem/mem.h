#ifndef _MEM_H_
#define _MEM_H_

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

// 32 Registers
extern volatile uint8_t regs[0x20];

// Defines where the registers are allocated in memory
#if PICO_RP2040 == 1
    asm(".equ regs, 0x20040000");
#elif PICO_RP2350 == 1
    asm(".equ regs, 0x20080000");
#else
    #error "Unknown microcontroller"
#endif

// Convenience macro to access a given register with a 5 bit address
#define REGS(addr) regs[(addr) & 0x1F]
// Convenience macro to access 2 registers and treat it like a 16 bit value
#define REGSW(addr) ((uint16_t *)&REGS(addr))[0]

#endif