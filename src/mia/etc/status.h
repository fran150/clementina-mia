#ifndef _MIA_ETC_STATUS_H_
#define _MIA_ETC_STATUS_H_

#include "mem/regs.h"
#include "hardware/sync.h"
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

// The status word lives here, in firmware RAM. MIA_STATUS ($FFEA-$FFEB) is
// only a copy, written while MIA is in normal mode: in loader mode those bytes
// belong to the 6502 loader program, so they are left alone and the copy is
// made when MIA enters normal mode. Firmware code (the console included) reads
// the word with mia_status_get(), never from the register.
extern volatile uint16_t mia_status_word;

// Returns the current status word.
static inline __force_inline uint16_t mia_status_get(void) {
    return mia_status_word;
}

// Every status change is a read-modify-write of the word plus a copy into
// MIA_STATUS, and it happens from both cores and from interrupts: core 1 sets
// CMD_RUNNING when the 6502 writes CMD_TRIGGER, the command interrupt clears
// it, and the main loop sets and clears the video, SD and audio flags. Without
// a lock, a main-loop update that read the word just before the command
// interrupt cleared CMD_RUNNING writes the stale bit back, CMD_RUNNING stays
// set forever and a 6502 waiting for it hangs. A striped SDK spin lock (which
// also masks interrupts on the calling core) makes each change atomic; it is
// held for a few instructions only.
#define MIA_STATUS_SPIN_LOCK_ID PICO_SPINLOCK_ID_STRIPED_FIRST

// Applies set and clear to the status word and copies it into MIA_STATUS when
// MIA is in normal mode. Caller holds the status lock.
static inline __force_inline void mia_status_apply_locked(uint16_t set, uint16_t clear) {
    uint16_t st = (uint16_t)((mia_status_word & (uint16_t)~clear) | set);
    mia_status_word = st;
    if (st & MIA_STAT_MASTER_MODE) {
        mia_regs->mia_status = st;
    }
}

// Clears the flags in clear, then sets the flags in set, atomically.
static inline __force_inline void mia_status_update(uint16_t set, uint16_t clear) {
    spin_lock_t *lock = spin_lock_instance(MIA_STATUS_SPIN_LOCK_ID);
    uint32_t save = spin_lock_blocking(lock);
    mia_status_apply_locked(set, clear);
    spin_unlock(lock, save);
}

// Stores a new status word and copies it into MIA_STATUS when MIA is in
// normal mode.
static inline __force_inline void mia_status_store(uint16_t st) {
    mia_status_update(st, 0xFFFFu);
}

// Clears every status flag. Used when MIA restarts the 6502 from the loader.
static inline __force_inline void mia_status_reset(void) {
    spin_lock_t *lock = spin_lock_instance(MIA_STATUS_SPIN_LOCK_ID);
    uint32_t save = spin_lock_blocking(lock);
    mia_status_word = 0;
    spin_unlock(lock, save);
}

// Sets the status flag
static inline __force_inline void mia_status_set_flag(uint16_t flag) {
    mia_status_update(flag, 0);
}

// Clears the status flag
static inline __force_inline void mia_status_clear_flag(uint16_t flag) {
    mia_status_update(0, flag);
}

#endif
