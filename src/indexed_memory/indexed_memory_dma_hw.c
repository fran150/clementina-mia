/**
 * Hardware DMA implementation for Raspberry Pi Pico
 */

#include "indexed_memory_dma.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include <stddef.h>

static int dma_channel = -1;
static void (*completion_callback)(void) = NULL;

/**
 * DMA interrupt handler
 */
static void dma_irq_handler(void) {
    if (dma_channel >= 0 && dma_channel_get_irq0_status(dma_channel)) {
        // Clear the interrupt
        dma_channel_acknowledge_irq0(dma_channel);
        
        // Call completion callback if set
        if (completion_callback) {
            completion_callback();
        }
    }
}

int indexed_memory_dma_init(void) {
    // Claim an unused DMA channel
    dma_channel = dma_claim_unused_channel(true);
    
    // Enable interrupt for this channel
    dma_channel_set_irq0_enabled(dma_channel, true);
    irq_set_exclusive_handler(DMA_IRQ_0, dma_irq_handler);
    irq_set_enabled(DMA_IRQ_0, true);
    
    return dma_channel;
}

void indexed_memory_dma_start_transfer(void* dst, const void* src, uint32_t count) {
    if (dma_channel < 0) {
        return;  // Not initialized
    }
    
    // Configure DMA channel
    dma_channel_config c = dma_channel_get_default_config(dma_channel);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_8);  // 8-bit transfers
    channel_config_set_read_increment(&c, true);             // Increment read address
    channel_config_set_write_increment(&c, true);            // Increment write address
    
    // Start DMA transfer
    dma_channel_configure(
        dma_channel,
        &c,
        dst,      // Write address
        src,      // Read address
        count,    // Transfer count
        true      // Start immediately
    );
}

bool indexed_memory_dma_is_busy(void) {
    if (dma_channel < 0) {
        return false;
    }
    return dma_channel_is_busy(dma_channel);
}

void indexed_memory_dma_set_completion_callback(void (*callback)(void)) {
    completion_callback = callback;
}
