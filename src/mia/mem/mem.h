#ifndef _MEM_H_
#define _MEM_H_

#include <stdint.h>

#define ONE_KB        1024
#define MIA_RAM_SIZE  128 * ONE_KB

extern volatile uint8_t mem[MIA_RAM_SIZE];

#endif
