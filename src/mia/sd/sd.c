#include "sd/sd.h"

#include <stdio.h>
#include <string.h>

#include "ff.h"
#include "hardware/gpio.h"
#include "hardware/spi.h"
#include "pico/stdlib.h"

#include "etc/err.h"
#include "etc/status.h"
#include "irq/irq.h"
#include "mem/indexes.h"
#include "mem/mem.h"
#include "video/video_dirty.h"

#ifndef MIA_SD_SPI_SLOW_BAUD
#define MIA_SD_SPI_SLOW_BAUD 400000u
#endif

#ifndef MIA_SD_SPI_FAST_BAUD
#define MIA_SD_SPI_FAST_BAUD 12000000u
#endif

#ifndef MIA_SD_SPI_INSTANCE
#define MIA_SD_SPI_INSTANCE 0
#endif

#ifndef MIA_SD_MISO_PIN
#define MIA_SD_MISO_PIN 0
#endif

#ifndef MIA_SD_CS_PIN
#define MIA_SD_CS_PIN 1
#endif

#ifndef MIA_SD_SCK_PIN
#define MIA_SD_SCK_PIN 2
#endif

#ifndef MIA_SD_MOSI_PIN
#define MIA_SD_MOSI_PIN 3
#endif

#if MIA_SD_SPI_INSTANCE == 0
#define MIA_SD_SPI_PORT spi0
#else
#define MIA_SD_SPI_PORT spi1
#endif

#define SD_CMD0_GO_IDLE_STATE 0u
#define SD_CMD8_SEND_IF_COND 8u
#define SD_CMD9_SEND_CSD 9u
#define SD_CMD16_SET_BLOCKLEN 16u
#define SD_CMD17_READ_SINGLE_BLOCK 17u
#define SD_CMD24_WRITE_BLOCK 24u
#define SD_CMD55_APP_CMD 55u
#define SD_CMD58_READ_OCR 58u
#define SD_ACMD41_SD_SEND_OP_COND 41u

#define SD_DATA_TOKEN 0xFEu
#define SD_WRITE_ACCEPTED 0x05u
#define SD_JOB_CHUNK_SIZE 512u

#ifndef MIA_SD_SERVICE_BUDGET_US
#define MIA_SD_SERVICE_BUDGET_US 1000u
#endif

typedef enum {
    sd_request_none = 0,
    sd_request_init = MIA_CMD_SD_INIT,
    sd_request_read_sector = MIA_CMD_SD_READ_SECTOR,
    sd_request_write_sector = MIA_CMD_SD_WRITE_SECTOR,
    sd_request_get_info = MIA_CMD_SD_GET_INFO,
    sd_request_mount = MIA_CMD_FS_MOUNT,
    sd_request_opendir = MIA_CMD_FS_OPENDIR,
    sd_request_readdir = MIA_CMD_FS_READDIR,
    sd_request_open = MIA_CMD_FS_OPEN,
    sd_request_read = MIA_CMD_FS_READ,
    sd_request_close = MIA_CMD_FS_CLOSE,
    sd_request_load = MIA_CMD_FS_LOAD_TO_MIA_RAM,
    sd_request_write = MIA_CMD_FS_WRITE,
    sd_request_sync = MIA_CMD_FS_SYNC,
    sd_request_seek = MIA_CMD_FS_SEEK,
    sd_request_stat = MIA_CMD_FS_STAT,
    sd_request_mkdir = MIA_CMD_FS_MKDIR,
    sd_request_delete = MIA_CMD_FS_DELETE,
    sd_request_rename = MIA_CMD_FS_RENAME,
    sd_request_get_free = MIA_CMD_FS_GET_FREE,
    sd_request_save = MIA_CMD_FS_SAVE_FROM_MIA_RAM,
} sd_request_t;

typedef enum {
    sd_job_none = 0,
    sd_job_load,
    sd_job_save,
} sd_job_type_t;

typedef struct {
    sd_job_type_t type;
    FIL file;
    uint32_t addr;
    uint32_t limit;
    uint32_t total;
} sd_job_state_t;

static volatile uint8_t sd_pending_request;
static volatile bool sd_request_pending;

static bool sd_spi_ready;
static bool sd_initialized;
static bool sd_mounted;
static bool sd_file_open;
static bool sd_dir_open;
static bool sd_eof;
static uint8_t sd_last_error;
static uint8_t sd_last_fatfs_result;
static uint8_t sd_type;
static uint8_t sd_current_open_mode;
static uint32_t sd_sectors;

static FATFS sd_fatfs;
static FIL sd_file;
static DIR sd_dir;
static sd_job_state_t sd_job;
static uint8_t sd_job_buffer[SD_JOB_CHUNK_SIZE];

static void sd_configure_indexes(void);
static void sd_configure_index(uint8_t index_id, uint32_t start, uint32_t length);
static void sd_publish_state(void);
static void sd_finish(bool ok, uint8_t error, FRESULT fatfs_result, bool fs_event);
static void sd_set_busy(bool busy);
static void sd_service_job(void);

static uint16_t sd_read_u16(uint32_t offset) {
    return (uint16_t)mem[offset] | ((uint16_t)mem[offset + 1u] << 8);
}

static uint32_t sd_read_u24(uint32_t offset) {
    return (uint32_t)mem[offset] |
           ((uint32_t)mem[offset + 1u] << 8) |
           ((uint32_t)mem[offset + 2u] << 16);
}

static uint32_t sd_read_u32(uint32_t offset) {
    return (uint32_t)mem[offset] |
           ((uint32_t)mem[offset + 1u] << 8) |
           ((uint32_t)mem[offset + 2u] << 16) |
           ((uint32_t)mem[offset + 3u] << 24);
}

static void sd_write_u16(uint32_t offset, uint16_t value) {
    mem[offset] = (uint8_t)value;
    mem[offset + 1u] = (uint8_t)(value >> 8);
}

static void sd_write_u32(uint32_t offset, uint32_t value) {
    mem[offset] = (uint8_t)value;
    mem[offset + 1u] = (uint8_t)(value >> 8);
    mem[offset + 2u] = (uint8_t)(value >> 16);
    mem[offset + 3u] = (uint8_t)(value >> 24);
}

static uint8_t sd_xfer(uint8_t value) {
    uint8_t in;
    spi_write_read_blocking(MIA_SD_SPI_PORT, &value, &in, 1);
    return in;
}

static void sd_select(void) {
    gpio_put(MIA_SD_CS_PIN, false);
    (void)sd_xfer(0xFFu);
}

static void sd_deselect(void) {
    gpio_put(MIA_SD_CS_PIN, true);
    (void)sd_xfer(0xFFu);
}

static bool sd_wait_ready(uint32_t timeout_ms) {
    absolute_time_t deadline = make_timeout_time_ms(timeout_ms);
    do {
        if (sd_xfer(0xFFu) == 0xFFu) {
            return true;
        }
        tight_loop_contents();
    } while (!time_reached(deadline));

    return false;
}

static uint8_t sd_command(uint8_t cmd, uint32_t arg, uint8_t crc) {
    if (!sd_wait_ready(500)) {
        return 0xFFu;
    }

    uint8_t packet[6] = {
        (uint8_t)(0x40u | cmd),
        (uint8_t)(arg >> 24),
        (uint8_t)(arg >> 16),
        (uint8_t)(arg >> 8),
        (uint8_t)arg,
        crc,
    };

    spi_write_blocking(MIA_SD_SPI_PORT, packet, sizeof(packet));

    for (uint8_t i = 0; i < 10u; i++) {
        uint8_t response = sd_xfer(0xFFu);
        if ((response & 0x80u) == 0) {
            return response;
        }
    }

    return 0xFFu;
}

static bool sd_read_token_data(uint8_t *buffer, uint32_t len, uint32_t timeout_ms) {
    absolute_time_t deadline = make_timeout_time_ms(timeout_ms);
    uint8_t token;

    do {
        token = sd_xfer(0xFFu);
        if (token == SD_DATA_TOKEN) {
            spi_read_blocking(MIA_SD_SPI_PORT, 0xFFu, buffer, len);
            (void)sd_xfer(0xFFu);
            (void)sd_xfer(0xFFu);
            return true;
        }
        if (token != 0xFFu) {
            return false;
        }
        tight_loop_contents();
    } while (!time_reached(deadline));

    return false;
}

static bool sd_read_csd(uint8_t csd[16]) {
    bool ok = false;

    sd_select();
    uint8_t response = sd_command(SD_CMD9_SEND_CSD, 0, 0xFFu);
    if (response == 0) {
        ok = sd_read_token_data(csd, 16u, 500);
    }
    sd_deselect();

    return ok;
}

static uint32_t sd_decode_csd_sector_count(const uint8_t csd[16]) {
    uint8_t csd_structure = csd[0] >> 6;

    if (csd_structure == 1u) {
        uint32_t c_size = ((uint32_t)(csd[7] & 0x3Fu) << 16) |
                          ((uint32_t)csd[8] << 8) |
                          (uint32_t)csd[9];
        return (c_size + 1u) * 1024u;
    }

    if (csd_structure == 0u) {
        uint32_t read_bl_len = csd[5] & 0x0Fu;
        uint32_t c_size = ((uint32_t)(csd[6] & 0x03u) << 10) |
                          ((uint32_t)csd[7] << 2) |
                          ((uint32_t)(csd[8] & 0xC0u) >> 6);
        uint32_t c_size_mult = ((uint32_t)(csd[9] & 0x03u) << 1) |
                               ((uint32_t)(csd[10] & 0x80u) >> 7);
        uint64_t block_len = 1ull << read_bl_len;
        uint64_t block_count = (uint64_t)(c_size + 1u) << (c_size_mult + 2u);
        return (uint32_t)((block_count * block_len) / MIA_SD_SECTOR_SIZE);
    }

    return 0;
}

static void sd_spi_init_once(void) {
    if (sd_spi_ready) {
        return;
    }

    spi_init(MIA_SD_SPI_PORT, MIA_SD_SPI_SLOW_BAUD);
    gpio_set_function(MIA_SD_MISO_PIN, GPIO_FUNC_SPI);
    gpio_set_function(MIA_SD_SCK_PIN, GPIO_FUNC_SPI);
    gpio_set_function(MIA_SD_MOSI_PIN, GPIO_FUNC_SPI);

    gpio_init(MIA_SD_CS_PIN);
    gpio_set_dir(MIA_SD_CS_PIN, GPIO_OUT);
    gpio_put(MIA_SD_CS_PIN, true);

    sd_spi_ready = true;
}

bool mia_sd_card_init(void) {
    sd_spi_init_once();

    sd_initialized = false;
    sd_mounted = false;
    sd_file_open = false;
    sd_dir_open = false;
    sd_eof = false;
    sd_current_open_mode = MIA_FS_OPEN_READ;
    memset(&sd_job, 0, sizeof(sd_job));
    sd_type = MIA_SD_CARD_NONE;
    sd_sectors = 0;
    mia_status_clear_flag(MIA_STAT_SD_PRESENT | MIA_STAT_FS_MOUNTED);

    spi_set_baudrate(MIA_SD_SPI_PORT, MIA_SD_SPI_SLOW_BAUD);
    sd_deselect();
    for (uint8_t i = 0; i < 10u; i++) {
        (void)sd_xfer(0xFFu);
    }

    bool v2 = false;
    sd_select();
    uint8_t response = sd_command(SD_CMD0_GO_IDLE_STATE, 0, 0x95u);
    sd_deselect();
    if (response != 0x01u) {
        sd_publish_state();
        return false;
    }

    sd_select();
    response = sd_command(SD_CMD8_SEND_IF_COND, 0x000001AAu, 0x87u);
    uint8_t r7[4] = {
        sd_xfer(0xFFu),
        sd_xfer(0xFFu),
        sd_xfer(0xFFu),
        sd_xfer(0xFFu),
    };
    sd_deselect();
    if (response == 0x01u && r7[2] == 0x01u && r7[3] == 0xAAu) {
        v2 = true;
    }

    absolute_time_t deadline = make_timeout_time_ms(2000);
    do {
        sd_select();
        response = sd_command(SD_CMD55_APP_CMD, 0, 0xFFu);
        sd_deselect();
        if (response > 0x01u) {
            continue;
        }

        sd_select();
        response = sd_command(SD_ACMD41_SD_SEND_OP_COND, v2 ? 0x40000000u : 0, 0xFFu);
        sd_deselect();
        if (response == 0) {
            break;
        }
        sleep_ms(1);
    } while (!time_reached(deadline));

    if (response != 0) {
        sd_publish_state();
        return false;
    }

    uint8_t ocr[4] = {0};
    sd_select();
    response = sd_command(SD_CMD58_READ_OCR, 0, 0xFFu);
    if (response == 0) {
        for (uint8_t i = 0; i < 4u; i++) {
            ocr[i] = sd_xfer(0xFFu);
        }
    }
    sd_deselect();
    if (response != 0) {
        sd_publish_state();
        return false;
    }

    bool high_capacity = v2 && ((ocr[0] & 0x40u) != 0);
    if (!high_capacity) {
        sd_select();
        response = sd_command(SD_CMD16_SET_BLOCKLEN, MIA_SD_SECTOR_SIZE, 0xFFu);
        sd_deselect();
        if (response != 0) {
            sd_publish_state();
            return false;
        }
    }

    sd_initialized = true;
    sd_type = high_capacity ? MIA_SD_CARD_SDHC : (v2 ? MIA_SD_CARD_SD_V2 : MIA_SD_CARD_SD_V1);

    uint8_t csd[16];
    if (sd_read_csd(csd)) {
        sd_sectors = sd_decode_csd_sector_count(csd);
    }

    spi_set_baudrate(MIA_SD_SPI_PORT, MIA_SD_SPI_FAST_BAUD);
    mia_status_set_flag(MIA_STAT_SD_PRESENT);
    sd_publish_state();
    return true;
}

static uint32_t sd_lba_to_card_addr(uint32_t lba) {
    if (sd_type == MIA_SD_CARD_SDHC) {
        return lba;
    }

    return lba * MIA_SD_SECTOR_SIZE;
}

bool mia_sd_block_read(uint32_t lba, uint8_t *buffer) {
    if (!sd_initialized) {
        return false;
    }

    bool ok = false;
    sd_select();
    uint8_t response = sd_command(SD_CMD17_READ_SINGLE_BLOCK, sd_lba_to_card_addr(lba), 0xFFu);
    if (response == 0) {
        ok = sd_read_token_data(buffer, MIA_SD_SECTOR_SIZE, 500);
    }
    sd_deselect();

    return ok;
}

bool mia_sd_block_write(uint32_t lba, const uint8_t *buffer) {
    if (!sd_initialized) {
        return false;
    }

    bool ok = false;
    sd_select();
    uint8_t response = sd_command(SD_CMD24_WRITE_BLOCK, sd_lba_to_card_addr(lba), 0xFFu);
    if (response == 0 && sd_wait_ready(500)) {
        (void)sd_xfer(SD_DATA_TOKEN);
        spi_write_blocking(MIA_SD_SPI_PORT, buffer, MIA_SD_SECTOR_SIZE);
        (void)sd_xfer(0xFFu);
        (void)sd_xfer(0xFFu);

        uint8_t data_response = sd_xfer(0xFFu) & 0x1Fu;
        ok = (data_response == SD_WRITE_ACCEPTED) && sd_wait_ready(1000);
    }
    sd_deselect();

    return ok;
}

bool mia_sd_is_initialized(void) {
    return sd_initialized;
}

uint8_t mia_sd_card_type(void) {
    return sd_type;
}

uint32_t mia_sd_sector_count(void) {
    return sd_sectors;
}

static void sd_clear_buffers(void) {
    memset(&mem[MIA_SD_CONTROL_OFFSET], 0, MIA_SD_CONTROL_SIZE);
    memset(&mem[MIA_SD_SECTOR_OFFSET], 0, MIA_SD_SECTOR_SIZE);
    memset(&mem[MIA_FS_PATH_OFFSET], 0, MIA_FS_PATH_SIZE);
    memset(&mem[MIA_FS_DIR_ENTRY_OFFSET], 0, MIA_FS_DIR_ENTRY_SIZE);
    memset(&mem[MIA_FS_TRANSFER_OFFSET], 0, MIA_FS_TRANSFER_SIZE);
}

void mia_sd_init(void) {
    mia_sd_reset_runtime_state();
}

void mia_sd_reset_runtime_state(void) {
    sd_request_pending = false;
    sd_pending_request = sd_request_none;
    if (sd_job.type != sd_job_none) {
        f_close(&sd_job.file);
    }
    memset(&sd_job, 0, sizeof(sd_job));
    sd_mounted = false;
    sd_file_open = false;
    sd_dir_open = false;
    sd_eof = false;
    sd_current_open_mode = MIA_FS_OPEN_READ;
    sd_last_error = 0;
    sd_last_fatfs_result = FR_OK;

    f_mount(NULL, "0:", 0);
    memset(&sd_fatfs, 0, sizeof(sd_fatfs));
    memset(&sd_file, 0, sizeof(sd_file));
    memset(&sd_dir, 0, sizeof(sd_dir));

    sd_clear_buffers();
    mem[MIA_SD_CONTROL_OFFSET + MIA_SD_CONTROL_VERSION] = MIA_SD_VERSION;
    sd_write_u16(MIA_SD_CONTROL_OFFSET + MIA_SD_CONTROL_REQUEST_LEN_L, MIA_FS_TRANSFER_SIZE);

    mia_status_clear_flag(MIA_STAT_SD_BUSY | MIA_STAT_FS_MOUNTED);
    if (sd_initialized) {
        mia_status_set_flag(MIA_STAT_SD_PRESENT);
    } else {
        mia_status_clear_flag(MIA_STAT_SD_PRESENT);
    }

    sd_configure_indexes();
    sd_publish_state();
}

bool mia_sd_request(uint8_t command) {
    switch (command) {
        case MIA_CMD_SD_INIT:
        case MIA_CMD_SD_READ_SECTOR:
        case MIA_CMD_SD_WRITE_SECTOR:
        case MIA_CMD_SD_GET_INFO:
        case MIA_CMD_FS_MOUNT:
        case MIA_CMD_FS_OPENDIR:
        case MIA_CMD_FS_READDIR:
        case MIA_CMD_FS_OPEN:
        case MIA_CMD_FS_READ:
        case MIA_CMD_FS_CLOSE:
        case MIA_CMD_FS_LOAD_TO_MIA_RAM:
        case MIA_CMD_FS_WRITE:
        case MIA_CMD_FS_SYNC:
        case MIA_CMD_FS_SEEK:
        case MIA_CMD_FS_STAT:
        case MIA_CMD_FS_MKDIR:
        case MIA_CMD_FS_DELETE:
        case MIA_CMD_FS_RENAME:
        case MIA_CMD_FS_GET_FREE:
        case MIA_CMD_FS_SAVE_FROM_MIA_RAM:
            break;
        default:
            return false;
    }

    if (sd_request_pending || (mia_regs->mia_status & MIA_STAT_SD_BUSY)) {
        error_push(ERROR_SD_BUSY);
        mia_irq_set_flag(IRQ_SD_ERROR);
        return false;
    }

    sd_pending_request = command;
    __dmb();
    sd_request_pending = true;
    sd_set_busy(true);
    return true;
}

static void sd_set_last_error(uint8_t error, FRESULT fatfs_result) {
    sd_last_error = error;
    sd_last_fatfs_result = (uint8_t)fatfs_result;
    mem[MIA_SD_CONTROL_OFFSET + MIA_SD_CONTROL_LAST_ERROR] = error;
    mem[MIA_SD_CONTROL_OFFSET + MIA_SD_CONTROL_FATFS_RESULT] = (uint8_t)fatfs_result;
}

static void sd_set_busy(bool busy) {
    if (busy) {
        mia_status_set_flag(MIA_STAT_SD_BUSY);
    } else {
        mia_status_clear_flag(MIA_STAT_SD_BUSY);
    }
    sd_publish_state();
}

static void sd_finish(bool ok, uint8_t error, FRESULT fatfs_result, bool fs_event) {
    sd_set_busy(false);

    if (ok) {
        sd_set_last_error(0, fatfs_result);
        mia_irq_set_flag(IRQ_SD_DONE);
    } else {
        sd_set_last_error(error, fatfs_result);
        error_push(error);
        mia_irq_set_flag(IRQ_SD_ERROR);
    }

    if (fs_event) {
        mia_irq_set_flag(IRQ_FS_EVENT);
    }

    sd_publish_state();
}

static void sd_publish_state(void) {
    uint8_t status = 0;

    if (sd_initialized) {
        status |= MIA_SD_STATUS_PRESENT | MIA_SD_STATUS_INITIALIZED;
    }
    if (sd_mounted) {
        status |= MIA_SD_STATUS_MOUNTED;
    }
    if (mia_regs->mia_status & MIA_STAT_SD_BUSY) {
        status |= MIA_SD_STATUS_BUSY;
    }
    if (sd_file_open) {
        status |= MIA_SD_STATUS_FILE_OPEN;
    }
    if (sd_dir_open) {
        status |= MIA_SD_STATUS_DIR_OPEN;
    }
    if (sd_eof) {
        status |= MIA_SD_STATUS_EOF;
    }
    if (sd_last_error != 0) {
        status |= MIA_SD_STATUS_ERROR;
    }

    mem[MIA_SD_CONTROL_OFFSET + MIA_SD_CONTROL_STATUS] = status;
    mem[MIA_SD_CONTROL_OFFSET + MIA_SD_CONTROL_LAST_ERROR] = sd_last_error;
    mem[MIA_SD_CONTROL_OFFSET + MIA_SD_CONTROL_CARD_TYPE] = sd_type;
    mem[MIA_SD_CONTROL_OFFSET + MIA_SD_CONTROL_EOF] = sd_eof ? 1u : 0u;
    mem[MIA_SD_CONTROL_OFFSET + MIA_SD_CONTROL_FILE_HANDLE] = sd_file_open ? 1u : 0u;
    mem[MIA_SD_CONTROL_OFFSET + MIA_SD_CONTROL_FATFS_RESULT] = sd_last_fatfs_result;
    sd_write_u32(MIA_SD_CONTROL_OFFSET + MIA_SD_CONTROL_CARD_SECTORS0, sd_sectors);

    if (sd_mounted) {
        mia_status_set_flag(MIA_STAT_FS_MOUNTED);
    } else {
        mia_status_clear_flag(MIA_STAT_FS_MOUNTED);
    }
}

static uint32_t sd_control_lba(void) {
    return sd_read_u32(MIA_SD_CONTROL_OFFSET + MIA_SD_CONTROL_LBA0);
}

static uint16_t sd_control_request_len(void) {
    return sd_read_u16(MIA_SD_CONTROL_OFFSET + MIA_SD_CONTROL_REQUEST_LEN_L);
}

static uint32_t sd_control_dest_addr(void) {
    return sd_read_u24(MIA_SD_CONTROL_OFFSET + MIA_SD_CONTROL_DEST_ADDR_L) & MIA_RAM_MASK;
}

static uint32_t sd_control_file_pos(void) {
    return sd_read_u32(MIA_SD_CONTROL_OFFSET + MIA_SD_CONTROL_FILE_POS0);
}

static uint32_t sd_control_transfer_len(void) {
    return sd_read_u32(MIA_SD_CONTROL_OFFSET + MIA_SD_CONTROL_TRANSFER_LEN0);
}

static bool sd_open_mode_to_fatfs(uint8_t open_mode, BYTE *fatfs_mode) {
    switch (open_mode) {
        case MIA_FS_OPEN_READ:
            *fatfs_mode = FA_READ;
            return true;
        case MIA_FS_OPEN_WRITE_CREATE:
            *fatfs_mode = FA_WRITE | FA_CREATE_ALWAYS;
            return true;
        case MIA_FS_OPEN_WRITE_APPEND:
            *fatfs_mode = FA_WRITE | FA_OPEN_APPEND;
            return true;
        case MIA_FS_OPEN_READ_WRITE:
            *fatfs_mode = FA_READ | FA_WRITE | FA_OPEN_ALWAYS;
            return true;
        default:
            return false;
    }
}

static void sd_prepare_fatfs_path_from(uint32_t offset, uint32_t size, char out[MIA_FS_PATH_SIZE + 3u]) {
    const char *in = (const char *)&mem[offset];
    uint32_t out_i = 0;

    out[out_i++] = '0';
    out[out_i++] = ':';
    if (in[0] == '\0') {
        out[out_i++] = '/';
    }

    for (uint32_t i = 0; i < size - 1u && in[i] != '\0' && out_i < MIA_FS_PATH_SIZE + 2u; i++) {
        out[out_i++] = in[i] == '\\' ? '/' : in[i];
    }
    out[out_i] = '\0';
}

static void sd_prepare_fatfs_path(char out[MIA_FS_PATH_SIZE + 3u]) {
    sd_prepare_fatfs_path_from(MIA_FS_PATH_OFFSET, MIA_FS_PATH_SIZE, out);
}

static void sd_prepare_fatfs_path2(char out[MIA_FS_PATH_SIZE + 3u]) {
    sd_prepare_fatfs_path_from(MIA_FS_PATH2_OFFSET, MIA_FS_PATH2_SIZE, out);
}

static void sd_clear_dir_entry(void) {
    memset(&mem[MIA_FS_DIR_ENTRY_OFFSET], 0, MIA_FS_DIR_ENTRY_SIZE);
}

static void sd_fill_dir_entry(const FILINFO *info) {
    sd_clear_dir_entry();

    const char *name = info->fname;
    uint32_t name_len = 0;
    while (name[name_len] != '\0' && name_len < MIA_FS_DIR_ENTRY_SIZE - MIA_FS_DIR_NAME - 1u) {
        mem[MIA_FS_DIR_ENTRY_OFFSET + MIA_FS_DIR_NAME + name_len] = (uint8_t)name[name_len];
        name_len++;
    }

    mem[MIA_FS_DIR_ENTRY_OFFSET + MIA_FS_DIR_ATTR] = info->fattrib;
    mem[MIA_FS_DIR_ENTRY_OFFSET + MIA_FS_DIR_NAME_LEN] = (uint8_t)name_len;
    sd_write_u32(MIA_FS_DIR_ENTRY_OFFSET + MIA_FS_DIR_SIZE0, (uint32_t)info->fsize);
    sd_write_u16(MIA_FS_DIR_ENTRY_OFFSET + MIA_FS_DIR_DATE_L, info->fdate);
    sd_write_u16(MIA_FS_DIR_ENTRY_OFFSET + MIA_FS_DIR_TIME_L, info->ftime);
}

static void sd_update_file_position(void) {
    if (!sd_file_open) {
        sd_write_u32(MIA_SD_CONTROL_OFFSET + MIA_SD_CONTROL_FILE_SIZE0, 0);
        sd_write_u32(MIA_SD_CONTROL_OFFSET + MIA_SD_CONTROL_FILE_POS0, 0);
        return;
    }

    sd_write_u32(MIA_SD_CONTROL_OFFSET + MIA_SD_CONTROL_FILE_SIZE0, (uint32_t)f_size(&sd_file));
    sd_write_u32(MIA_SD_CONTROL_OFFSET + MIA_SD_CONTROL_FILE_POS0, (uint32_t)f_tell(&sd_file));
}

static bool sd_mount_filesystem(FRESULT *out_result) {
    FRESULT fr = f_mount(&sd_fatfs, "0:", 1);
    *out_result = fr;
    sd_mounted = (fr == FR_OK);
    return sd_mounted;
}

static bool sd_require_mounted(FRESULT *out_result) {
    if (sd_mounted) {
        *out_result = FR_OK;
        return true;
    }

    return sd_mount_filesystem(out_result);
}

static void sd_job_update_progress(void) {
    sd_write_u16(MIA_SD_CONTROL_OFFSET + MIA_SD_CONTROL_RESULT_LEN_L, (uint16_t)sd_job.total);
    sd_write_u32(MIA_SD_CONTROL_OFFSET + MIA_SD_CONTROL_FILE_POS0, sd_job.total);
}

static void sd_finish_job(bool ok, uint8_t error, FRESULT fr) {
    sd_job_type_t type = sd_job.type;

    if (ok && type == sd_job_load) {
        sd_eof = f_eof(&sd_job.file) != 0;
    }
    if (ok && type == sd_job_save) {
        sd_write_u32(MIA_SD_CONTROL_OFFSET + MIA_SD_CONTROL_FILE_SIZE0, (uint32_t)f_size(&sd_job.file));
    }

    FRESULT close_fr = f_close(&sd_job.file);
    if (ok && close_fr != FR_OK) {
        ok = false;
        error = ERROR_FS_CLOSE_FAILED;
        fr = close_fr;
    }

    memset(&sd_job, 0, sizeof(sd_job));
    sd_finish(ok, error, fr, true);
}

static bool sd_start_load_job(uint32_t dest, uint32_t max_len, uint8_t *out_error, FRESULT *out_result) {
    char path[MIA_FS_PATH_SIZE + 3u];
    sd_prepare_fatfs_path(path);

    memset(&sd_job, 0, sizeof(sd_job));
    FRESULT fr = f_open(&sd_job.file, path, FA_READ);
    if (fr != FR_OK) {
        *out_result = fr;
        *out_error = ERROR_FS_OPEN_FAILED;
        return false;
    }

    sd_job.type = sd_job_load;
    sd_job.addr = dest;
    sd_job.limit = max_len;
    sd_job.total = 0;
    sd_eof = false;

    sd_write_u16(MIA_SD_CONTROL_OFFSET + MIA_SD_CONTROL_RESULT_LEN_L, 0);
    sd_write_u32(MIA_SD_CONTROL_OFFSET + MIA_SD_CONTROL_FILE_SIZE0, (uint32_t)f_size(&sd_job.file));
    sd_write_u32(MIA_SD_CONTROL_OFFSET + MIA_SD_CONTROL_FILE_POS0, 0);
    *out_result = FR_OK;
    *out_error = 0;
    return true;
}

static bool sd_start_save_job(uint32_t source, uint32_t length, uint8_t *out_error, FRESULT *out_result) {
    if (source > MIA_RAM_SIZE || length > MIA_RAM_SIZE - source) {
        *out_result = FR_INVALID_PARAMETER;
        *out_error = ERROR_FS_INVALID_REQUEST;
        return false;
    }

    uint8_t open_mode = mem[MIA_SD_CONTROL_OFFSET + MIA_SD_CONTROL_OPEN_MODE];
    BYTE fatfs_mode = 0;
    if (!sd_open_mode_to_fatfs(open_mode, &fatfs_mode) || (fatfs_mode & FA_WRITE) == 0) {
        *out_result = FR_DENIED;
        *out_error = ERROR_FS_INVALID_REQUEST;
        return false;
    }

    char path[MIA_FS_PATH_SIZE + 3u];
    sd_prepare_fatfs_path(path);

    memset(&sd_job, 0, sizeof(sd_job));
    FRESULT fr = f_open(&sd_job.file, path, fatfs_mode);
    if (fr != FR_OK) {
        *out_result = fr;
        *out_error = ERROR_FS_OPEN_FAILED;
        return false;
    }

    sd_job.type = sd_job_save;
    sd_job.addr = source;
    sd_job.limit = length;
    sd_job.total = 0;
    sd_eof = false;

    sd_write_u16(MIA_SD_CONTROL_OFFSET + MIA_SD_CONTROL_RESULT_LEN_L, 0);
    sd_write_u32(MIA_SD_CONTROL_OFFSET + MIA_SD_CONTROL_FILE_SIZE0, (uint32_t)f_size(&sd_job.file));
    sd_write_u32(MIA_SD_CONTROL_OFFSET + MIA_SD_CONTROL_FILE_POS0, 0);
    *out_result = FR_OK;
    *out_error = 0;
    return true;
}

static void sd_job_step_load(void) {
    if (sd_job.addr >= MIA_RAM_SIZE || (sd_job.limit != 0 && sd_job.total >= sd_job.limit)) {
        sd_finish_job(true, 0, FR_OK);
        return;
    }

    uint32_t remaining_ram = MIA_RAM_SIZE - sd_job.addr;
    uint32_t remaining_limit = sd_job.limit == 0 ? remaining_ram : sd_job.limit - sd_job.total;
    uint32_t chunk = SD_JOB_CHUNK_SIZE;

    if (remaining_ram == 0 || remaining_limit == 0) {
        sd_finish_job(true, 0, FR_OK);
        return;
    }
    if (chunk > remaining_ram) {
        chunk = remaining_ram;
    }
    if (chunk > remaining_limit) {
        chunk = remaining_limit;
    }

    UINT br = 0;
    FRESULT fr = f_read(&sd_job.file, sd_job_buffer, chunk, &br);
    if (fr != FR_OK) {
        sd_finish_job(false, ERROR_FS_READ_FAILED, fr);
        return;
    }

    for (UINT i = 0; i < br; i++) {
        uint32_t offset = sd_job.addr + i;
        mem[offset] = sd_job_buffer[i];
        mia_video_mark_dirty(offset);
    }

    sd_job.addr += br;
    sd_job.total += br;
    sd_job_update_progress();

    if (br == 0 || br < chunk || f_eof(&sd_job.file) != 0 ||
        (sd_job.limit != 0 && sd_job.total >= sd_job.limit)) {
        sd_finish_job(true, 0, FR_OK);
    }
}

static void sd_job_step_save(void) {
    if (sd_job.total >= sd_job.limit) {
        sd_finish_job(true, 0, FR_OK);
        return;
    }

    uint32_t remaining = sd_job.limit - sd_job.total;
    uint32_t chunk = remaining > SD_JOB_CHUNK_SIZE ? SD_JOB_CHUNK_SIZE : remaining;
    memcpy(sd_job_buffer, &mem[sd_job.addr], chunk);

    UINT bw = 0;
    FRESULT fr = f_write(&sd_job.file, sd_job_buffer, chunk, &bw);
    if (bw != 0) {
        sd_job.addr += bw;
        sd_job.total += bw;
        sd_job_update_progress();
    }

    if (fr != FR_OK || bw != chunk) {
        sd_finish_job(false, ERROR_FS_WRITE_FAILED, fr);
        return;
    }

    if (sd_job.total >= sd_job.limit) {
        sd_finish_job(true, 0, FR_OK);
    }
}

static void sd_service_job(void) {
    uint32_t start = time_us_32();

    do {
        if (sd_job.type == sd_job_load) {
            sd_job_step_load();
        } else if (sd_job.type == sd_job_save) {
            sd_job_step_save();
        } else {
            return;
        }
    } while (sd_job.type != sd_job_none &&
             (uint32_t)(time_us_32() - start) < MIA_SD_SERVICE_BUDGET_US);

    sd_publish_state();
}

void mia_sd_service(void) {
    if (sd_job.type != sd_job_none) {
        sd_service_job();
        return;
    }

    if (!sd_request_pending) {
        return;
    }

    uint8_t command = sd_pending_request;
    sd_pending_request = sd_request_none;
    sd_request_pending = false;

    bool ok = true;
    uint8_t error = 0;
    FRESULT fr = FR_OK;
    bool fs_event = command >= MIA_CMD_FS_MOUNT;
    bool deferred = false;

    sd_eof = false;
    sd_write_u16(MIA_SD_CONTROL_OFFSET + MIA_SD_CONTROL_RESULT_LEN_L, 0);

    switch ((sd_request_t)command) {
        case sd_request_init:
            ok = mia_sd_card_init();
            error = ERROR_SD_INIT_FAILED;
            break;

        case sd_request_read_sector:
            ok = mia_sd_block_read(sd_control_lba(), &mem[MIA_SD_SECTOR_OFFSET]);
            error = sd_initialized ? ERROR_SD_READ_FAILED : ERROR_SD_NOT_READY;
            if (ok) {
                sd_write_u16(MIA_SD_CONTROL_OFFSET + MIA_SD_CONTROL_RESULT_LEN_L, MIA_SD_SECTOR_SIZE);
            }
            break;

        case sd_request_write_sector:
            ok = mia_sd_block_write(sd_control_lba(), &mem[MIA_SD_SECTOR_OFFSET]);
            error = sd_initialized ? ERROR_SD_WRITE_FAILED : ERROR_SD_NOT_READY;
            if (ok) {
                sd_write_u16(MIA_SD_CONTROL_OFFSET + MIA_SD_CONTROL_RESULT_LEN_L, MIA_SD_SECTOR_SIZE);
            }
            break;

        case sd_request_get_info:
            ok = sd_initialized;
            error = ERROR_SD_NOT_READY;
            break;

        case sd_request_mount:
            ok = sd_mount_filesystem(&fr);
            error = ERROR_FS_MOUNT_FAILED;
            break;

        case sd_request_opendir: {
            if (!sd_require_mounted(&fr)) {
                ok = false;
                error = ERROR_FS_MOUNT_FAILED;
                break;
            }
            if (sd_dir_open) {
                f_closedir(&sd_dir);
                sd_dir_open = false;
            }
            char path[MIA_FS_PATH_SIZE + 3u];
            sd_prepare_fatfs_path(path);
            fr = f_opendir(&sd_dir, path);
            ok = (fr == FR_OK);
            sd_dir_open = ok;
            error = ERROR_FS_DIR_FAILED;
            sd_clear_dir_entry();
            break;
        }

        case sd_request_readdir: {
            if (!sd_dir_open) {
                ok = false;
                error = ERROR_FS_DIR_FAILED;
                fr = FR_INVALID_OBJECT;
                break;
            }
            FILINFO info;
            fr = f_readdir(&sd_dir, &info);
            ok = (fr == FR_OK);
            error = ERROR_FS_DIR_FAILED;
            if (ok) {
                if (info.fname[0] == '\0') {
                    sd_eof = true;
                    sd_clear_dir_entry();
                } else {
                    sd_fill_dir_entry(&info);
                }
            }
            break;
        }

        case sd_request_open: {
            if (!sd_require_mounted(&fr)) {
                ok = false;
                error = ERROR_FS_MOUNT_FAILED;
                break;
            }
            uint8_t open_mode = mem[MIA_SD_CONTROL_OFFSET + MIA_SD_CONTROL_OPEN_MODE];
            BYTE fatfs_mode = 0;
            if (!sd_open_mode_to_fatfs(open_mode, &fatfs_mode)) {
                ok = false;
                error = ERROR_FS_INVALID_REQUEST;
                fr = FR_DENIED;
                break;
            }
            if (sd_file_open) {
                f_close(&sd_file);
                sd_file_open = false;
                sd_current_open_mode = MIA_FS_OPEN_READ;
            }
            char path[MIA_FS_PATH_SIZE + 3u];
            sd_prepare_fatfs_path(path);
            fr = f_open(&sd_file, path, fatfs_mode);
            ok = (fr == FR_OK);
            sd_file_open = ok;
            sd_current_open_mode = ok ? open_mode : MIA_FS_OPEN_READ;
            error = ERROR_FS_OPEN_FAILED;
            sd_update_file_position();
            break;
        }

        case sd_request_read: {
            if (!sd_file_open) {
                ok = false;
                error = ERROR_FS_NO_FILE_OPEN;
                fr = FR_INVALID_OBJECT;
                break;
            }
            uint16_t requested = sd_control_request_len();
            if (requested == 0 || requested > MIA_FS_TRANSFER_SIZE) {
                requested = MIA_FS_TRANSFER_SIZE;
            }
            UINT br = 0;
            fr = f_read(&sd_file, &mem[MIA_FS_TRANSFER_OFFSET], requested, &br);
            ok = (fr == FR_OK);
            error = ERROR_FS_READ_FAILED;
            if (ok) {
                sd_write_u16(MIA_SD_CONTROL_OFFSET + MIA_SD_CONTROL_RESULT_LEN_L, (uint16_t)br);
                sd_eof = f_eof(&sd_file) != 0;
            }
            sd_update_file_position();
            break;
        }

        case sd_request_write: {
            if (!sd_file_open) {
                ok = false;
                error = ERROR_FS_NO_FILE_OPEN;
                fr = FR_INVALID_OBJECT;
                break;
            }
            uint16_t requested = sd_control_request_len();
            if (requested == 0 || requested > MIA_FS_TRANSFER_SIZE) {
                requested = MIA_FS_TRANSFER_SIZE;
            }
            UINT bw = 0;
            fr = f_write(&sd_file, &mem[MIA_FS_TRANSFER_OFFSET], requested, &bw);
            ok = (fr == FR_OK) && (bw == requested);
            error = ERROR_FS_WRITE_FAILED;
            sd_write_u16(MIA_SD_CONTROL_OFFSET + MIA_SD_CONTROL_RESULT_LEN_L, (uint16_t)bw);
            sd_update_file_position();
            break;
        }

        case sd_request_sync:
            if (!sd_file_open) {
                ok = false;
                error = ERROR_FS_NO_FILE_OPEN;
                fr = FR_INVALID_OBJECT;
                break;
            }
            fr = f_sync(&sd_file);
            ok = (fr == FR_OK);
            error = ERROR_FS_SYNC_FAILED;
            sd_update_file_position();
            break;

        case sd_request_seek:
            if (!sd_file_open) {
                ok = false;
                error = ERROR_FS_NO_FILE_OPEN;
                fr = FR_INVALID_OBJECT;
                break;
            }
            fr = f_lseek(&sd_file, sd_control_file_pos());
            ok = (fr == FR_OK);
            error = ERROR_FS_SEEK_FAILED;
            sd_update_file_position();
            break;

        case sd_request_stat: {
            if (!sd_require_mounted(&fr)) {
                ok = false;
                error = ERROR_FS_MOUNT_FAILED;
                break;
            }
            char path[MIA_FS_PATH_SIZE + 3u];
            FILINFO info;
            sd_prepare_fatfs_path(path);
            fr = f_stat(path, &info);
            ok = (fr == FR_OK);
            error = ERROR_FS_STAT_FAILED;
            if (ok) {
                sd_fill_dir_entry(&info);
                sd_write_u16(MIA_SD_CONTROL_OFFSET + MIA_SD_CONTROL_RESULT_LEN_L, MIA_FS_DIR_ENTRY_SIZE);
            }
            break;
        }

        case sd_request_mkdir: {
            if (!sd_require_mounted(&fr)) {
                ok = false;
                error = ERROR_FS_MOUNT_FAILED;
                break;
            }
            char path[MIA_FS_PATH_SIZE + 3u];
            sd_prepare_fatfs_path(path);
            fr = f_mkdir(path);
            ok = (fr == FR_OK);
            error = ERROR_FS_MKDIR_FAILED;
            break;
        }

        case sd_request_delete: {
            if (!sd_require_mounted(&fr)) {
                ok = false;
                error = ERROR_FS_MOUNT_FAILED;
                break;
            }
            char path[MIA_FS_PATH_SIZE + 3u];
            sd_prepare_fatfs_path(path);
            fr = f_unlink(path);
            ok = (fr == FR_OK);
            error = ERROR_FS_DELETE_FAILED;
            break;
        }

        case sd_request_rename: {
            if (!sd_require_mounted(&fr)) {
                ok = false;
                error = ERROR_FS_MOUNT_FAILED;
                break;
            }
            char old_path[MIA_FS_PATH_SIZE + 3u];
            char new_path[MIA_FS_PATH_SIZE + 3u];
            sd_prepare_fatfs_path(old_path);
            sd_prepare_fatfs_path2(new_path);
            fr = f_rename(old_path, new_path);
            ok = (fr == FR_OK);
            error = ERROR_FS_RENAME_FAILED;
            break;
        }

        case sd_request_get_free: {
            if (!sd_require_mounted(&fr)) {
                ok = false;
                error = ERROR_FS_MOUNT_FAILED;
                break;
            }
            FATFS *fs = NULL;
            DWORD free_clusters = 0;
            fr = f_getfree("0:", &free_clusters, &fs);
            ok = (fr == FR_OK && fs != NULL);
            error = ERROR_FS_FREE_FAILED;
            if (ok) {
                uint32_t total_clusters = fs->n_fatent > 2u ? (uint32_t)(fs->n_fatent - 2u) : 0u;
                uint16_t cluster_sectors = fs->csize;
                sd_write_u32(MIA_SD_CONTROL_OFFSET + MIA_SD_CONTROL_FREE_CLUSTERS0, (uint32_t)free_clusters);
                sd_write_u32(MIA_SD_CONTROL_OFFSET + MIA_SD_CONTROL_TOTAL_CLUSTERS0, total_clusters);
                sd_write_u16(MIA_SD_CONTROL_OFFSET + MIA_SD_CONTROL_CLUSTER_SECTORS_L, cluster_sectors);
                sd_write_u16(MIA_SD_CONTROL_OFFSET + MIA_SD_CONTROL_RESULT_LEN_L, 10u);
            }
            break;
        }

        case sd_request_close:
            if (sd_file_open) {
                fr = f_close(&sd_file);
                ok = (fr == FR_OK);
                error = ERROR_FS_CLOSE_FAILED;
            }
            if (sd_dir_open) {
                FRESULT dir_fr = f_closedir(&sd_dir);
                if (ok && dir_fr != FR_OK) {
                    fr = dir_fr;
                    ok = false;
                    error = ERROR_FS_DIR_FAILED;
                }
            }
            sd_file_open = false;
            sd_dir_open = false;
            sd_current_open_mode = MIA_FS_OPEN_READ;
            sd_update_file_position();
            break;

        case sd_request_load: {
            if (!sd_require_mounted(&fr)) {
                ok = false;
                error = ERROR_FS_MOUNT_FAILED;
                break;
            }
            uint32_t max_len = sd_control_request_len();
            ok = sd_start_load_job(sd_control_dest_addr(), max_len, &error, &fr);
            deferred = ok;
            break;
        }

        case sd_request_save: {
            if (!sd_require_mounted(&fr)) {
                ok = false;
                error = ERROR_FS_MOUNT_FAILED;
                break;
            }
            ok = sd_start_save_job(sd_control_dest_addr(), sd_control_transfer_len(), &error, &fr);
            deferred = ok;
            break;
        }

        case sd_request_none:
        default:
            ok = false;
            error = ERROR_FS_INVALID_REQUEST;
            fr = FR_INVALID_PARAMETER;
            break;
    }

    if (!deferred) {
        sd_finish(ok, error, fr, fs_event);
    }
}

void mia_sd_print_summary(void) {
    const char *card = "none";
    if (sd_type == MIA_SD_CARD_SD_V1) {
        card = "SD v1";
    } else if (sd_type == MIA_SD_CARD_SD_V2) {
        card = "SD v2";
    } else if (sd_type == MIA_SD_CARD_SDHC) {
        card = "SDHC/SDXC";
    }

    printf("  SD:     %s  card:%s  fs:%s\n",
           sd_initialized ? "ready" : "not ready",
           card,
           sd_mounted ? "mounted" : "unmounted");
}

void mia_sd_print_status(void) {
    printf("SD/FS:\n");
    printf("  state:     initialized:%s mounted:%s busy:%s file:%s dir:%s job:%u eof:%s\n",
           sd_initialized ? "yes" : "no",
           sd_mounted ? "yes" : "no",
           (mia_regs->mia_status & MIA_STAT_SD_BUSY) ? "yes" : "no",
           sd_file_open ? "open" : "closed",
           sd_dir_open ? "open" : "closed",
           (unsigned)sd_job.type,
           sd_eof ? "yes" : "no");
    printf("  card:      type:%u sectors:%lu capacity:%lu MiB\n",
           (unsigned)sd_type,
           (unsigned long)sd_sectors,
           (unsigned long)(sd_sectors / 2048u));
    printf("  pins:      SPI%u MISO:%u CS:%u SCK:%u MOSI:%u fast:%u Hz\n",
           (unsigned)MIA_SD_SPI_INSTANCE,
           (unsigned)MIA_SD_MISO_PIN,
           (unsigned)MIA_SD_CS_PIN,
           (unsigned)MIA_SD_SCK_PIN,
           (unsigned)MIA_SD_MOSI_PIN,
           (unsigned)MIA_SD_SPI_FAST_BAUD);
    printf("  block:     control:$%05X-$%05X sector:$%05X-$%05X path:$%05X-$%05X\n",
           MIA_SD_CONTROL_OFFSET,
           MIA_SD_CONTROL_OFFSET + MIA_SD_CONTROL_SIZE - 1u,
           MIA_SD_SECTOR_OFFSET,
           MIA_SD_SECTOR_OFFSET + MIA_SD_SECTOR_SIZE - 1u,
           MIA_FS_PATH_OFFSET,
           MIA_FS_PATH_OFFSET + MIA_FS_PATH_SIZE - 1u);
    printf("             dir:$%05X-$%05X transfer:$%05X-$%05X\n",
           MIA_FS_DIR_ENTRY_OFFSET,
           MIA_FS_DIR_ENTRY_OFFSET + MIA_FS_DIR_ENTRY_SIZE - 1u,
           MIA_FS_TRANSFER_OFFSET,
           MIA_FS_TRANSFER_OFFSET + MIA_FS_TRANSFER_SIZE - 1u);
    printf("  indexes:   control:$%02X sector:$%02X path:$%02X dir:$%02X transfer:$%02X path2:$%02X\n",
           MIA_SD_INDEX_CONTROL,
           MIA_SD_INDEX_SECTOR,
           MIA_FS_INDEX_PATH,
           MIA_FS_INDEX_DIR_ENTRY,
           MIA_FS_INDEX_TRANSFER,
           MIA_FS_INDEX_PATH2);
    printf("  control:   status:0x%02X last-error:0x%02X fatfs:%u lba:%lu req:%u result:%u dest:$%05lX\n",
           (unsigned)mem[MIA_SD_CONTROL_OFFSET + MIA_SD_CONTROL_STATUS],
           (unsigned)sd_last_error,
           (unsigned)sd_last_fatfs_result,
           (unsigned long)sd_control_lba(),
           (unsigned)sd_control_request_len(),
           (unsigned)sd_read_u16(MIA_SD_CONTROL_OFFSET + MIA_SD_CONTROL_RESULT_LEN_L),
           (unsigned long)sd_control_dest_addr());
    printf("  file:      size:%lu pos:%lu\n",
           (unsigned long)sd_read_u32(MIA_SD_CONTROL_OFFSET + MIA_SD_CONTROL_FILE_SIZE0),
           (unsigned long)sd_read_u32(MIA_SD_CONTROL_OFFSET + MIA_SD_CONTROL_FILE_POS0));
    printf("             mode:%u requested-open-mode:%u\n",
           (unsigned)sd_current_open_mode,
           (unsigned)mem[MIA_SD_CONTROL_OFFSET + MIA_SD_CONTROL_OPEN_MODE]);
    printf("  free:      clusters:%lu/%lu cluster-sectors:%u\n",
           (unsigned long)sd_read_u32(MIA_SD_CONTROL_OFFSET + MIA_SD_CONTROL_FREE_CLUSTERS0),
           (unsigned long)sd_read_u32(MIA_SD_CONTROL_OFFSET + MIA_SD_CONTROL_TOTAL_CLUSTERS0),
           (unsigned)sd_read_u16(MIA_SD_CONTROL_OFFSET + MIA_SD_CONTROL_CLUSTER_SECTORS_L));
    printf("  service:   chunk:%u budget:%u us transfer-len:%lu\n",
           (unsigned)SD_JOB_CHUNK_SIZE,
           (unsigned)MIA_SD_SERVICE_BUDGET_US,
           (unsigned long)sd_control_transfer_len());
}

static void sd_configure_indexes(void) {
    sd_configure_index(MIA_SD_INDEX_CONTROL, MIA_SD_CONTROL_OFFSET, MIA_SD_CONTROL_SIZE);
    sd_configure_index(MIA_SD_INDEX_SECTOR, MIA_SD_SECTOR_OFFSET, MIA_SD_SECTOR_SIZE);
    sd_configure_index(MIA_FS_INDEX_PATH, MIA_FS_PATH_OFFSET, MIA_FS_PATH_SIZE);
    sd_configure_index(MIA_FS_INDEX_DIR_ENTRY, MIA_FS_DIR_ENTRY_OFFSET, MIA_FS_DIR_ENTRY_SIZE);
    sd_configure_index(MIA_FS_INDEX_TRANSFER, MIA_FS_TRANSFER_OFFSET, MIA_FS_TRANSFER_SIZE);
    sd_configure_index(MIA_FS_INDEX_PATH2, MIA_FS_PATH2_OFFSET, MIA_FS_PATH2_SIZE);
}

static void sd_configure_index(uint8_t index_id, uint32_t start, uint32_t length) {
    idx[index_id].current_addr = start;
    idx[index_id].default_addr = start;
    idx[index_id].limit_addr = start + length;
    idx[index_id].step = 1u;
    idx[index_id].flags = (1u << IDX_FLAG_R_STP_ENA) |
                          (1u << IDX_FLAG_W_STP_ENA) |
                          (1u << IDX_FLAG_WRAP_ENA);
    idx[index_id].reserved = 0;
}
