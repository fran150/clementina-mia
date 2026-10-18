#ifndef _MIA_VIDEO_DIRTY_H_
#define _MIA_VIDEO_DIRTY_H_

#include <stdbool.h>
#include <stdint.h>

#include "hardware/sync.h"
#include "pico.h"

#define MIA_VIDEO_STATE_SIZE 68944u
#define MIA_VIDEO_PAGE_SIZE 32u
#define MIA_VIDEO_PAGE_SHIFT 5u
#define MIA_VIDEO_PAGE_COUNT 2155u
#define MIA_VIDEO_FIRST_SYNC_PAGE 1u
#define MIA_VIDEO_SYNC_PAGE_COUNT (MIA_VIDEO_PAGE_COUNT - MIA_VIDEO_FIRST_SYNC_PAGE)
#define MIA_VIDEO_SYNC_START MIA_VIDEO_PAGE_SIZE
#define MIA_VIDEO_DIRTY_MAP_SIZE 270u
#define MIA_VIDEO_HEADER_SIZE 32u
#define MIA_VIDEO_PAGE_RECORD_SIZE 34u
#define MIA_VIDEO_UDP_PAYLOAD_SIZE 512u
#define MIA_VIDEO_RECORDS_PER_CHUNK ((MIA_VIDEO_UDP_PAYLOAD_SIZE - MIA_VIDEO_HEADER_SIZE) / MIA_VIDEO_PAGE_RECORD_SIZE)
#define MIA_VIDEO_LAYOUT_VERSION 1u

#define MIA_VIDEO_LOCAL_VERSION_OFFSET 0x00000u
#define MIA_VIDEO_LOCAL_FRAME_ID_OFFSET 0x00004u
#define MIA_VIDEO_LOCAL_DIRTY_PAGES_OFFSET 0x00008u
#define MIA_VIDEO_RENDER_CONTROL_OFFSET 0x00020u
#define MIA_VIDEO_MODE_OFFSET 0x00020u
#define MIA_VIDEO_PALETTE_OFFSET 0x00100u
#define MIA_VIDEO_CHR_OFFSET 0x00200u
#define MIA_VIDEO_BG_NT_OFFSET 0x0C200u
#define MIA_VIDEO_BG_ATTR_OFFSET 0x0E140u
#define MIA_VIDEO_OVERLAY_NT_OFFSET 0x10080u
#define MIA_VIDEO_OVERLAY_ATTR_OFFSET 0x10468u
#define MIA_VIDEO_OAM_OFFSET 0x10850u

// Maps 0 and 1 take turns as core 1's active map and the pending map of a
// response. Map 2 is core 0's own: core 1 marks the active map without a lock,
// and a read-modify-write of the same byte from each core can lose a bit, so
// core 0 marks map 2 and merges it into the pending map when it takes a frame.
#define MIA_VIDEO_CORE0_DIRTY_MAP 2u

extern uint8_t mia_video_dirty_maps[3][MIA_VIDEO_DIRTY_MAP_SIZE];
extern volatile uint8_t mia_video_active_dirty_index;

extern volatile bool mia_video_rotate_request;
extern volatile bool mia_video_rotate_done;
extern volatile uint8_t mia_video_rotated_pending_index;

extern volatile bool mia_video_mark_all_request;
extern volatile bool mia_video_mark_all_done;

// Core 1 only: marks a 6502 write in the active map.
static inline __force_inline void __not_in_flash_func(mia_video_core1_mark_dirty)(uint32_t addr) {
    if (addr < MIA_VIDEO_SYNC_START || addr >= MIA_VIDEO_STATE_SIZE) {
        return;
    }

    uint32_t page = addr >> MIA_VIDEO_PAGE_SHIFT;
    uint8_t *dirty = mia_video_dirty_maps[mia_video_active_dirty_index];
    dirty[page >> 3] |= (uint8_t)(1u << (page & 7u));
}

// Core 0 only: marks a byte core 0 wrote itself in core 0's map. SD loads and
// the monitor mark from core 0's main loop, DMA completion and commands from its
// interrupts, so core 0 marks with interrupts off: otherwise an interrupt's mark
// could land between the main loop's read and write of the same byte.
static inline __force_inline void __not_in_flash_func(mia_video_mark_dirty)(uint32_t addr) {
    if (addr < MIA_VIDEO_SYNC_START || addr >= MIA_VIDEO_STATE_SIZE) {
        return;
    }

    uint32_t page = addr >> MIA_VIDEO_PAGE_SHIFT;
    uint8_t *dirty = mia_video_dirty_maps[MIA_VIDEO_CORE0_DIRTY_MAP];
    uint32_t irq = save_and_disable_interrupts();
    dirty[page >> 3] |= (uint8_t)(1u << (page & 7u));
    restore_interrupts(irq);
}

// Core 0 only: marks a range core 0 wrote itself in core 0's map, with
// interrupts off like mia_video_mark_dirty().
static inline __force_inline void __not_in_flash_func(mia_video_mark_dirty_range)(uint32_t start, uint32_t len) {
    if (len == 0) {
        return;
    }

    uint32_t end = start + len - 1u;
    if (end < start) {
        end = UINT32_MAX;
    }
    if (start >= MIA_VIDEO_STATE_SIZE || end < MIA_VIDEO_SYNC_START) {
        return;
    }

    if (start < MIA_VIDEO_SYNC_START) {
        start = MIA_VIDEO_SYNC_START;
    }
    if (end >= MIA_VIDEO_STATE_SIZE) {
        end = MIA_VIDEO_STATE_SIZE - 1u;
    }

    uint32_t first_page = start >> MIA_VIDEO_PAGE_SHIFT;
    uint32_t last_page = end >> MIA_VIDEO_PAGE_SHIFT;
    uint8_t *dirty = mia_video_dirty_maps[MIA_VIDEO_CORE0_DIRTY_MAP];
    uint32_t irq = save_and_disable_interrupts();

    for (uint32_t page = first_page; page <= last_page; page++) {
        dirty[page >> 3] |= (uint8_t)(1u << (page & 7u));
    }

    restore_interrupts(irq);
}

static inline __force_inline void __not_in_flash_func(mia_video_mark_all_active_dirty)(void) {
    uint8_t *dirty = mia_video_dirty_maps[mia_video_active_dirty_index];

    for (uint32_t i = 0; i < MIA_VIDEO_DIRTY_MAP_SIZE; i++) {
        dirty[i] = 0xFFu;
    }

    dirty[0] &= 0xFEu;
    dirty[MIA_VIDEO_DIRTY_MAP_SIZE - 1] = 0x07u;
}

static inline __force_inline void __not_in_flash_func(mia_video_core1_poll)(void) {
    if (mia_video_rotate_request) {
        uint8_t old_active = mia_video_active_dirty_index;
        mia_video_active_dirty_index = old_active ^ 1u;
        mia_video_rotated_pending_index = old_active;
        __dmb();
        mia_video_rotate_request = false;
        mia_video_rotate_done = true;
    }

    if (mia_video_mark_all_request) {
        mia_video_mark_all_active_dirty();
        __dmb();
        mia_video_mark_all_request = false;
        mia_video_mark_all_done = true;
    }
}

#endif
