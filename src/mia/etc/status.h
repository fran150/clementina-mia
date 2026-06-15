#ifndef _MIA_ETC_STATUS_H_
#define _MIA_ETC_STATUS_H_

#include "mem/regs.h"
#include <stdint.h>

#define MIA_STAT_MASTER_MODE         (1u << 0)  // MIA status: 0 = Bootloader, 1 = Normal
#define MIA_STAT_ERRORS              (1u << 1)  // Defines if there are errors in the queue
#define MIA_STAT_CMD_RUNNING         (1u << 2)  // Defines if a command is running
#define MIA_STAT_DMA_RUNNING         (1u << 3)  // Determines if DMA is running
#define MIA_STAT_SPEED_CHANGING      (1u << 4)  // PHI2 speed change is pending
#define MIA_STAT_VIDEO_FRAME_REQUESTED (1u << 5) // Video update request accepted and waiting for ACK
#define MIA_STAT_VIDEO_FRAME_SENT    (1u << 6)  // Initial video response send finished and waiting for ACK
#define MIA_STAT_EXEC_PAUSED         (1u << 7)  // PHI2 is stopped by the exec pause control
#define MIA_STAT_AUDIO_ACTIVE        (1u << 8)  // PWM PSG audio engine is running
#define MIA_STAT_SD_PRESENT          (1u << 9)  // SD card initialized successfully, not socket-detect state
#define MIA_STAT_SD_BUSY             (1u << 10) // SD/FS command is in progress
#define MIA_STAT_FS_MOUNTED          (1u << 11) // FAT filesystem is mounted

// Sets the status flag
static inline __force_inline void mia_status_set_flag(uint16_t flag) {
    mia_regs->mia_status |= flag;
}

// Clears the status flag
static inline __force_inline void mia_status_clear_flag(uint16_t flag) {
    mia_regs->mia_status &= ~flag;
}

#endif
