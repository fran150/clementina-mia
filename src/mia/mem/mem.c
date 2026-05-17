#include "mem.h"

static uint8_t mia_ram[MIA_RAM_SIZE] __attribute__((aligned(4096)));

// The actual pointer definition.
uint8_t *mem = mia_ram;

void mia_mem_init(void) {
    // RAM is statically reserved so the linker fails if the target cannot fit it.
}
