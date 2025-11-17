/**
 * Mock DMA implementation for unit testing
 * Simulates DMA behavior synchronously
 */

#include "indexed_memory_dma.h"
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

static bool dma_busy = false;
static void (*completion_callback)(void) = NULL;

int indexed_memory_dma_init(void) {
    printf("Mock DMA initialized\n");
    return 0;  // Mock channel 0
}

void indexed_memory_dma_start_transfer(void* dst, const void* src, uint32_t count) {
    dma_busy = true;
    
    // Perform synchronous copy
    memcpy(dst, src, count);
    
    // Simulate completion
    dma_busy = false;
    
    // Call completion callback if set
    if (completion_callback) {
        completion_callback();
    }
}

bool indexed_memory_dma_is_busy(void) {
    return dma_busy;
}

void indexed_memory_dma_set_completion_callback(void (*callback)(void)) {
    completion_callback = callback;
}
