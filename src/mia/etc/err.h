#ifndef _MIA_ETC_ERR_H_
#define _MIA_ETC_ERR_H_

#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>
#include "status.h"
#include "irq/irq.h"
#include "mem/regs.h"

#define ERROR_MIA_CANNOT_ALLOCATE_RAM   0x01        // MIA was not able to allocate enough ram on startup
#define ERROR_QUEUE_OVERFLOW            0x02        // Error queue overwrote one or more unread errors
#define ERROR_DMA_SIZE_ZERO             0x10        // Error when triggering DMA transfer with count in zero
#define ERROR_DMA_SRC_WILL_OVERFLOW     0x11        // Error when DMA + count of the source will overflow the memory max size
#define ERROR_DMA_TGT_WILL_OVERFLOW     0x12        // Error when DMA + count of the target will overflow the memory max size
#define ERROR_CMD_QUEUE_FULL            0x20        // Command trigger could not be queued for core 0
#define ERROR_CMD_UNKNOWN               0x21        // Unknown command id
#define ERROR_WIFI_INIT_FAILED          0x30        // CYW43/Wi-Fi chip initialization failed
#define ERROR_WIFI_CONNECT_FAILED       0x31        // STA connection failed
#define ERROR_VIDEO_UDP_ALLOC_FAILED    0x40        // Video UDP PCB allocation failed
#define ERROR_VIDEO_UDP_BIND_FAILED     0x41        // Video UDP bind failed
#define ERROR_INPUT_MODE_UNAVAILABLE    0x50        // Requested input mode is not available
#define ERROR_INPUT_PROBE_INVALID       0x51        // Requested input probe id is invalid
#define ERROR_INPUT_UDP_ALLOC_FAILED    0x52        // Input UDP PCB allocation failed
#define ERROR_INPUT_UDP_BIND_FAILED     0x53        // Input UDP bind failed
#define ERROR_AUDIO_QUEUE_OVERFLOW      0x60        // Audio register write queue overflowed
#define ERROR_SD_BUSY                   0x70        // SD/FS request could not start because another request is running
#define ERROR_SD_INIT_FAILED            0x71        // SD card initialization failed
#define ERROR_SD_NOT_READY              0x72        // SD card is not initialized
#define ERROR_SD_READ_FAILED            0x73        // Raw SD sector read failed
#define ERROR_SD_WRITE_FAILED           0x74        // Raw SD sector write failed
#define ERROR_FS_MOUNT_FAILED           0x78        // FAT filesystem mount failed
#define ERROR_FS_OPEN_FAILED            0x79        // FAT file open failed
#define ERROR_FS_READ_FAILED            0x7A        // FAT file read failed
#define ERROR_FS_CLOSE_FAILED           0x7B        // FAT file close failed
#define ERROR_FS_DIR_FAILED             0x7C        // FAT directory operation failed
#define ERROR_FS_INVALID_REQUEST        0x7D        // SD/FS request parameters are invalid
#define ERROR_FS_NO_FILE_OPEN           0x7E        // FAT file I/O requested with no open file
#define ERROR_FS_WRITE_FAILED           0x7F        // FAT file write failed
#define ERROR_FS_SEEK_FAILED            0x80        // FAT file seek failed
#define ERROR_FS_SYNC_FAILED            0x81        // FAT file sync failed
#define ERROR_FS_STAT_FAILED            0x82        // FAT stat failed
#define ERROR_FS_MKDIR_FAILED           0x83        // FAT mkdir failed
#define ERROR_FS_DELETE_FAILED          0x84        // FAT delete failed
#define ERROR_FS_RENAME_FAILED          0x85        // FAT rename failed
#define ERROR_FS_FREE_FAILED            0x86        // FAT free-space query failed

#define ERROR_DEFER_CMD_QUEUE_FULL      (1u << 0)
#define ERROR_DEFER_AUDIO_QUEUE_OVERFLOW (1u << 1)

extern volatile uint8_t _err_first;
extern volatile uint8_t _err_last;
extern volatile uint8_t _err_buf[16];
extern atomic_uint _err_deferred_flags;

void error_service(void);

// Clears the error queue, the CPU-visible register, and its status bit.
static inline __force_inline void error_reset(void) {
    _err_first = 0;
    _err_last = 0;
    atomic_store(&_err_deferred_flags, 0);
    mia_regs->mia_error = 0;
    mia_status_clear_flag(MIA_STAT_ERRORS);
}

// Pushes the error to the queue. If the queue is full, the oldest unread entry
// is discarded and the queued value becomes ERROR_QUEUE_OVERFLOW. This keeps
// producers non-blocking while making loss visible to the 6502.
//
// When the queue was empty the value is also
// preloaded into the CPU-visible MIA_ERROR register, so the very first read
// returns this error rather than a stale value (the read handler only advances
// to the *next* error). Producer side; runs on the core that detects the error.
static inline __force_inline void error_push(uint8_t error) {
    uint8_t next = (_err_last + 1) & 15;
    bool was_empty = (_err_first == _err_last);
    bool overwrote_head = false;

    if (next == _err_first) {
        _err_first = (_err_first + 1) & 15;
        error = ERROR_QUEUE_OVERFLOW;
        overwrote_head = true;
    }

    _err_buf[_err_last] = error;
    _err_last = next;
    mia_status_set_flag(MIA_STAT_ERRORS);
    mia_irq_set_flag(IRQ_ERROR);

    if (was_empty) {
        mia_regs->mia_error = error;
    } else if (overwrote_head) {
        mia_regs->mia_error = _err_buf[_err_first];
    }
}

// Core 1 and other latency-sensitive contexts use this to ask the main service
// loop to enqueue an error later. It is intentionally lossy by flag; repeated
// identical failures collapse into one pending report.
static inline __force_inline void error_defer(uint32_t flags) {
    atomic_fetch_or(&_err_deferred_flags, flags);
}

// Called after the 6502 has read the current head error from MIA_ERROR. Advances
// the queue and loads the next error (or 0 when drained) into the register so the
// following read returns it. Returns the value loaded. Consumer side.
static inline __force_inline uint8_t error_consume(void) {
    if (_err_first != _err_last) {
        _err_first = (_err_first + 1) & 15;
    }

    uint8_t next;
    if (_err_first == _err_last) {
        mia_status_clear_flag(MIA_STAT_ERRORS);
        next = 0;
    } else {
        next = _err_buf[_err_first];
    }

    mia_regs->mia_error = next;
    return next;
}

#endif
