#include "mem/dma.h"
#include "mem.h"
#include "etc/err.h"
#include "etc/status.h"
#include "irq/irq.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "video/video_dirty.h"

int mia_dma_chan;
dma_channel_config config;

// A copy is a rectangle: rows of len bytes, each row starting src_stride
// further on than the last in the source and dst_stride further on in the
// destination. COPY_INDEXES is one row. The completion interrupt marks each
// row dirty and starts the next; IRQ_COMMAND is raised once, after the last.
typedef struct {
    uint32_t src_offset;
    uint32_t dst_offset;
    uint16_t len;
    uint16_t rows;          // rows still to copy, the running one included
    uint32_t src_stride;
    uint32_t dst_stride;
} mia_dma_job_t;

// Copies requested while one is still running wait here, in order, and the
// completion interrupt starts the next. Starting a copy on the busy channel
// would reprogram it mid-transfer and replace the range still to be marked
// dirty. Only the command handler and the completion interrupt touch the
// queue; both run on core 0 at the same priority, so neither preempts the
// other.
#define MIA_DMA_QUEUE_SIZE 8u

static mia_dma_job_t mia_dma_queue[MIA_DMA_QUEUE_SIZE];
static volatile uint8_t mia_dma_queue_head;
static volatile uint8_t mia_dma_queue_count;
static volatile bool mia_dma_busy;
static mia_dma_job_t mia_dma_running;

static void mia_dma_start_row(void) {
    dma_channel_configure(
        mia_dma_chan,
        &config,
        &mem[mia_dma_running.dst_offset],
        &mem[mia_dma_running.src_offset],
        mia_dma_running.len,
        true
    );
}

static void mia_dma_start(const mia_dma_job_t *job) {
    mia_dma_running = *job;
    mia_dma_busy = true;
    mia_dma_start_row();
}

// Callback used when DMA transfer is completed
void on_mia_dma_complete() {
    dma_irqn_acknowledge_channel(0, mia_dma_chan);
    mia_video_mark_dirty_range(mia_dma_running.dst_offset, mia_dma_running.len);

    if (--mia_dma_running.rows != 0) {
        mia_dma_running.src_offset += mia_dma_running.src_stride;
        mia_dma_running.dst_offset += mia_dma_running.dst_stride;
        mia_dma_start_row();
        return;
    }

    // The asynchronous copy command has finished; signal command completion.
    mia_irq_set_flag(IRQ_COMMAND);

    if (mia_dma_queue_count != 0) {
        mia_dma_job_t job = mia_dma_queue[mia_dma_queue_head];
        mia_dma_queue_head = (uint8_t)((mia_dma_queue_head + 1u) % MIA_DMA_QUEUE_SIZE);
        mia_dma_queue_count--;
        mia_dma_start(&job);
        return;
    }

    mia_dma_busy = false;
    mia_status_clear_flag(MIA_STAT_DMA_RUNNING);
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
    return mia_dma_rect_init(src_offset, dst_offset, len, 1u, 0u, 0u);
}

// Starts a rectangle copy: rows of len bytes, the source advancing src_stride
// and the destination dst_stride from one row to the next. A source stride of
// 0 repeats the first row, which is how rectangle fills work.
bool mia_dma_rect_init(uint32_t src_offset, uint32_t dst_offset, uint16_t len,
                       uint16_t rows, uint32_t src_stride, uint32_t dst_stride) {
    // Prevent zero-length transfers
    if (len == 0 || rows == 0) {
        error_push(ERROR_DMA_SIZE_ZERO);
        return false;
    }

    // Bounds check the last row: start + (rows - 1) * stride + len must fit
    // in MIA RAM. 64-bit math keeps the product from wrapping.
    uint64_t src_end = (uint64_t)src_offset + (uint64_t)(rows - 1u) * src_stride + len;
    if (src_offset >= MIA_RAM_SIZE || src_end > MIA_RAM_SIZE) {
        error_push(ERROR_DMA_SRC_WILL_OVERFLOW);
        return false;
    }

    uint64_t dst_end = (uint64_t)dst_offset + (uint64_t)(rows - 1u) * dst_stride + len;
    if (dst_offset >= MIA_RAM_SIZE || dst_end > MIA_RAM_SIZE) {
        error_push(ERROR_DMA_TGT_WILL_OVERFLOW);
        return false;
    }

    mia_dma_job_t job = {src_offset, dst_offset, len, rows, src_stride, dst_stride};

    if (!mia_dma_busy) {
        mia_status_set_flag(MIA_STAT_DMA_RUNNING);
        mia_dma_start(&job);
        return true;
    }

    if (mia_dma_queue_count == MIA_DMA_QUEUE_SIZE) {
        error_push(ERROR_DMA_QUEUE_FULL);
        return false;
    }

    uint8_t tail = (uint8_t)((mia_dma_queue_head + mia_dma_queue_count) % MIA_DMA_QUEUE_SIZE);
    mia_dma_queue[tail] = job;
    mia_dma_queue_count++;
    return true;
}
