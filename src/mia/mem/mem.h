#include <stdint.h>

#define MIA_RAM_SIZE (256 * 1024)
#define MIA_RAM_MASK (MIA_RAM_SIZE - 1)

// RAM memory allocated on the MIA
extern uint8_t *mem;

// Initialize memory
void mia_mem_init(void);
