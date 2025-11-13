/**
 * DMA abstraction layer for indexed memory operations
 * Provides a clean interface that can be mocked for testing
 */

#ifndef INDEXED_MEMORY_DMA_H
#define INDEXED_MEMORY_DMA_H

#include <stdint.h>
#include <stdbool.h>

/**
 * Initialize DMA for memory operations
 * Returns the DMA channel number, or -1 on failure
 */
int indexed_memory_dma_init(void);

/**
 * Start an asynchronous DMA transfer
 * 
 * @param dst Destination address
 * @param src Source address
 * @param count Number of bytes to transfer
 */
void indexed_memory_dma_start_transfer(void* dst, const void* src, uint32_t count);

/**
 * Check if DMA is currently busy
 * Returns true if transfer in progress, false if idle
 */
bool indexed_memory_dma_is_busy(void);

/**
 * Wait for current DMA transfer to complete (blocking)
 */
void indexed_memory_dma_wait_for_completion(void);

/**
 * Set callback for DMA completion
 * The callback will be invoked from interrupt context when transfer completes
 */
void indexed_memory_dma_set_completion_callback(void (*callback)(void));

#endif // INDEXED_MEMORY_DMA_H
