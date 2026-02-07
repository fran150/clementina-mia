#include "mem.h"

#include <stdlib.h>
#include <malloc.h>

#include "mia/etc/err.h"

// The actual pointer definition
uint8_t *mem = NULL;

// Initializes MIA memory. This should succeed always. If it fails it will push ERROR_MIA_CANNOT_ALLOCATE_RAM in the error queue and 
// MIA is not expected to work correctly.
void mia_mem_init(void) {
    mem = (uint8_t *)memalign(4096, MIA_RAM_SIZE);
    
    if (mem == NULL) {
        error_push(ERROR_MIA_CANNOT_ALLOCATE_RAM);
    }
}