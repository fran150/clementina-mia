#ifndef _MIA_MEM_DMA_H_
#define _MIA_MEM_DMA_H_

#include <stdint.h>

void mia_dma_init();
bool mia_dma_transfer_init(uint32_t src_offset, uint32_t dst_offset, uint16_t len);

#endif