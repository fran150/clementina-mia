#include "regs.h"

#if PICO_RP2350 == 1
    volatile mia_regs_t * const mia_regs = (volatile mia_regs_t *)0x20080000;
#else
    volatile mia_regs_t * const mia_regs = (volatile mia_regs_t *)0x20040000;
#endif