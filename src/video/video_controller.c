/**
 * Video Controller Implementation
 * Graphics memory management and frame processing
 */

#include "video_controller.h"
#include "hardware/gpio_mapping.h"

// Graphics memory structures
static uint8_t character_tables[CHARACTER_TABLES][CHARACTERS_PER_TABLE][BYTES_PER_CHARACTER];
static uint16_t palette_banks[PALETTE_BANKS][COLORS_PER_PALETTE];
static uint8_t nametables[NAMETABLE_BUFFERS][NAMETABLE_HEIGHT][NAMETABLE_WIDTH];
static uint8_t palette_tables[PALETTE_TABLE_BUFFERS][NAMETABLE_HEIGHT][NAMETABLE_WIDTH];
static uint8_t oam_data[MAX_SPRITES][BYTES_PER_SPRITE];

// Video state
static uint8_t active_character_table __attribute__((unused)) = 0;
static uint8_t active_nametable __attribute__((unused)) = 0;
static uint8_t active_palette_table __attribute__((unused)) = 0;
static bool frame_ready = false;

// PPU registers
static uint8_t ppu_control = 0;
static uint8_t ppu_status = 0;
static uint16_t ppu_oam_addr = 0;

void video_controller_init_core0(void) {
    // Initialize video memory to default values
    // Clear all character tables
    for (int table = 0; table < CHARACTER_TABLES; table++) {
        for (int char_idx = 0; char_idx < CHARACTERS_PER_TABLE; char_idx++) {
            for (int byte = 0; byte < BYTES_PER_CHARACTER; byte++) {
                character_tables[table][char_idx][byte] = 0;
            }
        }
    }
    
    // Initialize palette banks with default colors
    for (int bank = 0; bank < PALETTE_BANKS; bank++) {
        for (int color = 0; color < COLORS_PER_PALETTE; color++) {
            palette_banks[bank][color] = 0x0000;  // Black
        }
    }
    
    // Clear nametables
    for (int buffer = 0; buffer < NAMETABLE_BUFFERS; buffer++) {
        for (int y = 0; y < NAMETABLE_HEIGHT; y++) {
            for (int x = 0; x < NAMETABLE_WIDTH; x++) {
                nametables[buffer][y][x] = 0;
            }
        }
    }
    
    // Clear palette tables
    for (int buffer = 0; buffer < PALETTE_TABLE_BUFFERS; buffer++) {
        for (int y = 0; y < NAMETABLE_HEIGHT; y++) {
            for (int x = 0; x < NAMETABLE_WIDTH; x++) {
                palette_tables[buffer][y][x] = 0;
            }
        }
    }
    
    // Clear OAM data
    for (int sprite = 0; sprite < MAX_SPRITES; sprite++) {
        for (int byte = 0; byte < BYTES_PER_SPRITE; byte++) {
            oam_data[sprite][byte] = 0;
        }
    }
    
    // Initialize PPU registers
    ppu_control = 0;
    ppu_status = 0;
    ppu_oam_addr = 0;
    
    frame_ready = false;
}

void video_controller_init_core1(void) {
    // Core 1 initialization for video processing
    // This will be expanded in later tasks
}

void video_controller_process(void) {
    // Core 1 video processing
    // Prepare frame data for Wi-Fi transmission
    video_controller_prepare_frame_data();
}

bool video_controller_handle_read(uint16_t address, uint8_t *data) {
    if (!data) return false;
    
    if (address < VIDEO_PALETTE_BASE + 0x100) {
        // Palette bank access
        // Implementation will be expanded in later tasks
        *data = 0x00;
        return true;
    } else if (address >= VIDEO_CHAR_BASE && address < VIDEO_CHAR_BASE + 0x100) {
        // Character table access
        // Implementation will be expanded in later tasks
        *data = 0x00;
        return true;
    } else if (address >= VIDEO_OAM_BASE && address < VIDEO_OAM_BASE + 0x100) {
        // OAM access
        // Implementation will be expanded in later tasks
        *data = 0x00;
        return true;
    } else if (address >= VIDEO_PPU_BASE && address <= PPU_OAM_DMA) {
        // PPU register access
        switch (address) {
            case PPU_STATUS:
                *data = ppu_status;
                return true;
            case PPU_OAM_DATA:
                if (ppu_oam_addr < MAX_SPRITES * BYTES_PER_SPRITE) {
                    uint8_t sprite = ppu_oam_addr / BYTES_PER_SPRITE;
                    uint8_t byte_offset = ppu_oam_addr % BYTES_PER_SPRITE;
                    *data = oam_data[sprite][byte_offset];
                } else {
                    *data = 0x00;
                }
                return true;
            default:
                *data = 0x00;
                return true;
        }
    }
    
    return false;
}

bool video_controller_handle_write(uint16_t address, uint8_t data) {
    if (address < VIDEO_PALETTE_BASE + 0x100) {
        // Palette bank access
        // Implementation will be expanded in later tasks
        return true;
    } else if (address >= VIDEO_CHAR_BASE && address < VIDEO_CHAR_BASE + 0x100) {
        // Character table access
        // Implementation will be expanded in later tasks
        return true;
    } else if (address >= VIDEO_OAM_BASE && address < VIDEO_OAM_BASE + 0x100) {
        // OAM access
        // Implementation will be expanded in later tasks
        return true;
    } else if (address >= VIDEO_PPU_BASE && address <= PPU_OAM_DMA) {
        // PPU register access
        switch (address) {
            case PPU_CONTROL:
                ppu_control = data;
                return true;
            case PPU_OAM_ADDR:
                ppu_oam_addr = data;
                return true;
            case PPU_OAM_DATA:
                if (ppu_oam_addr < MAX_SPRITES * BYTES_PER_SPRITE) {
                    uint8_t sprite = ppu_oam_addr / BYTES_PER_SPRITE;
                    uint8_t byte_offset = ppu_oam_addr % BYTES_PER_SPRITE;
                    oam_data[sprite][byte_offset] = data;
                    ppu_oam_addr++;  // Auto-increment
                }
                return true;
            case PPU_OAM_DMA:
                // DMA transfer - implementation will be expanded in later tasks
                return true;
            default:
                return true;
        }
    }
    
    return false;
}

void video_controller_prepare_frame_data(void) {
    // Prepare frame data for Wi-Fi transmission
    // This will be expanded in later tasks
    frame_ready = true;
}

bool video_controller_is_frame_ready(void) {
    return frame_ready;
}