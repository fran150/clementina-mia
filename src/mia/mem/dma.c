#include "mem.h"
#include "etc/err.h"
#include "etc/status.h"
#include "irq/irq.h"
#include "hardware/dma.h"
#include "hardware/irq.h"

int mia_dma_chan;
dma_channel_config config;

// Callback used when DMA transfer is completed
void on_mia_dma_complete() {
    dma_irqn_acknowledge_channel(0, mia_dma_chan);
    mia_status_clear_flag(MIA_STAT_DMA_RUNNING);

    // The asynchronous copy command has finished; signal command completion.
    mia_irq_set_flag(IRQ_COMMAND);
}

// Initialize DMA channel for MIA RAM transfer
void mia_dma_init() {
    mia_dma_chan = dma_claim_unused_channel(true);
    config = dma_channel_get_default_config(mia_dma_chan);
    
    channel_config_set_transfer_data_size(&config, DMA_SIZE_8); // 8-bit for 6502
    channel_config_set_read_increment(&config, true);
    channel_config_set_write_increment(&config, true);
    
    irq_set_exclusive_handler(DMA_IRQ_0, on_mia_dma_complete);
    dma_channel_set_irq0_enabled(mia_dma_chan, true);
    
    irq_set_enabled(DMA_IRQ_0, true);
}

// Starts a trasnfer within the MIA from the source address to the target address of len bytes.
bool mia_dma_transfer_init(uint32_t src_offset, uint32_t dst_offset, uint16_t len) {
    // Prevent zero-length transfers
    if (len == 0) {
        error_push(ERROR_DMA_SIZE_ZERO);
        return false;
    }

    // Bounds check: Ensure start + length doesn't exceed MIA_RAM_SIZE
    // We check (A > TOTAL || B > TOTAL - A) to prevent integer overflow
    if (src_offset >= MIA_RAM_SIZE || len > MIA_RAM_SIZE - src_offset) {
        error_push(ERROR_DMA_SRC_WILL_OVERFLOW);
        return false;
    }

    if (dst_offset >= MIA_RAM_SIZE || len > MIA_RAM_SIZE - dst_offset) {
        error_push(ERROR_DMA_TGT_WILL_OVERFLOW);
        return false;
    }

    mia_status_set_flag(MIA_STAT_DMA_RUNNING);

    dma_channel_configure(
        mia_dma_chan,
        &config,       
        &mem[dst_offset],
        &mem[src_offset],
        len,             
        true             
    );

    return true;
}