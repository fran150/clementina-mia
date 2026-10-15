#ifndef _MIA_VIDEO_H_
#define _MIA_VIDEO_H_

#include <stdbool.h>
#include <stdint.h>

#include "video_dirty.h"

#ifndef MIA_VIDEO_UDP_PORT
#define MIA_VIDEO_UDP_PORT 6502u
#endif

#define MIA_CMD_VIDEO_FORCE_FULL_REFRESH 0x42u
#define MIA_CMD_VIDEO_SET_MODE 0x43u

/* Index IDs preconfigured at reset (the VIDX_* names in docs/video-output.md).
 * $73-$7F, $89-$8F and $BA-$BF are reserved for video. */
#define MIA_VIDEO_INDEX_LOCAL_CONTROL 0x70u
#define MIA_VIDEO_INDEX_FRAME_ID 0x71u
#define MIA_VIDEO_INDEX_LAST_RESPONSE_DIRTY_PAGES 0x72u

#define MIA_VIDEO_INDEX_RENDER_CONTROL 0x80u
#define MIA_VIDEO_INDEX_LAYER_ENABLE 0x81u
#define MIA_VIDEO_INDEX_BG_VIEWPORT 0x82u
#define MIA_VIDEO_INDEX_SCROLL_X 0x83u
#define MIA_VIDEO_INDEX_SCROLL_Y 0x84u
#define MIA_VIDEO_INDEX_BANK_SELECT 0x85u
#define MIA_VIDEO_INDEX_CHR_1BPP 0x86u
#define MIA_VIDEO_INDEX_BACKDROP_COLOR 0x87u
#define MIA_VIDEO_INDEX_OAM_LAST 0x88u

// First id of each run: palettes 0-15, CHR banks 0-7, BG tables 0-7, sprites 0-31.
#define MIA_VIDEO_INDEX_PALETTE_0 0x90u
#define MIA_VIDEO_INDEX_CHR_BANK_0 0xA0u
#define MIA_VIDEO_INDEX_BG_NT_0 0xA8u
#define MIA_VIDEO_INDEX_BG_ATTR_0 0xB0u
#define MIA_VIDEO_INDEX_OVERLAY_NT 0xB8u
#define MIA_VIDEO_INDEX_OVERLAY_ATTR 0xB9u
#define MIA_VIDEO_INDEX_OAM_SPRITE_0 0xC0u

void mia_video_init(void);
void mia_video_reset_runtime_state(void);
void mia_video_service(void);
void mia_video_report_errors(void);
void mia_video_print_summary(void);
void mia_video_print_status(void);

void mia_video_enable(void);
void mia_video_force_full_refresh(void);
void mia_video_set_mode(uint8_t mode);

#endif
