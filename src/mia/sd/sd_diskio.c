#include "ff.h"
#include "diskio.h"

#include "sd/sd.h"

DSTATUS disk_initialize(BYTE pdrv) {
    if (pdrv != 0) {
        return STA_NOINIT;
    }

    return mia_sd_card_init() ? 0 : STA_NOINIT;
}

DSTATUS disk_status(BYTE pdrv) {
    if (pdrv != 0 || !mia_sd_is_initialized()) {
        return STA_NOINIT;
    }

    return 0;
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count) {
    if (pdrv != 0 || buff == 0 || count == 0 || !mia_sd_is_initialized()) {
        return RES_PARERR;
    }

    for (UINT i = 0; i < count; i++) {
        if (!mia_sd_block_read((uint32_t)(sector + i), &buff[(uint32_t)i * MIA_SD_SECTOR_SIZE])) {
            return RES_ERROR;
        }
    }

    return RES_OK;
}

DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count) {
    if (pdrv != 0 || buff == 0 || count == 0 || !mia_sd_is_initialized()) {
        return RES_PARERR;
    }

    for (UINT i = 0; i < count; i++) {
        if (!mia_sd_block_write((uint32_t)(sector + i), &buff[(uint32_t)i * MIA_SD_SECTOR_SIZE])) {
            return RES_ERROR;
        }
    }

    return RES_OK;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff) {
    if (pdrv != 0 || !mia_sd_is_initialized()) {
        return RES_PARERR;
    }

    switch (cmd) {
        case CTRL_SYNC:
            return RES_OK;

        case GET_SECTOR_COUNT:
            if (buff == 0) {
                return RES_PARERR;
            }
            *(LBA_t *)buff = (LBA_t)mia_sd_sector_count();
            return RES_OK;

        case GET_BLOCK_SIZE:
            if (buff == 0) {
                return RES_PARERR;
            }
            *(DWORD *)buff = 1;
            return RES_OK;

        default:
            return RES_PARERR;
    }
}
