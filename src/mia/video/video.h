#ifndef _MIA_VIDEO_H_
#define _MIA_VIDEO_H_

#include <stdbool.h>
#include <stdint.h>

#include "video_dirty.h"

#ifndef MIA_VIDEO_UDP_PORT
#define MIA_VIDEO_UDP_PORT 6502u
#endif

void mia_video_init(void);
void mia_video_reset_runtime_state(void);
void mia_video_service(void);

void mia_video_enable(void);
void mia_video_force_full_refresh(void);
void mia_video_set_mode(uint8_t mode);

#endif
