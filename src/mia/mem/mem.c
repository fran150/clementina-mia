#include "mem.h"

volatile uint8_t mem[MIA_RAM_SIZE] __attribute__((aligned(4096)));