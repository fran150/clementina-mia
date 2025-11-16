/**
 * Video Controller System for MIA
 * Manages graphics memory and video output processing
 */

#ifndef VIDEO_CONTROLLER_H
#define VIDEO_CONTROLLER_H

#include <stdint.h>
#include <stdbool.h>

// Video system constants
#define SCREEN_WIDTH        320
#define SCREEN_HEIGHT       200
#define TILE_WIDTH          8
#define TILE_HEIGHT         8
#define NAMETABLE_WIDTH     40
#define NAMETABLE_HEIGHT    25

#define CHARACTER_TABLES    8
#define CHARACTERS_PER_TABLE 256
#define BYTES_PER_CHARACTER 24  // 8x8 pixels * 3 bits per pixel

#define PALETTE_BANKS       16
#define COLORS_PER_PALETTE  8
#define BYTES_PER_COLOR     2   // 16-bit RGB565

#define NAMETABLE_BUFFERS   4   // 2 active + 2 double buffer
#define PALETTE_TABLE_BUFFERS 2 // 1 active + 1 double buffer

#define MAX_SPRITES         256
#define BYTES_PER_SPRITE    4

// Memory-mapped I/O addresses (relative to video base)
#define VIDEO_PALETTE_BASE  0x0000  // $D000-$D0FF
#define VIDEO_CHAR_BASE     0x0100  // $D100-$D1FF
#define VIDEO_OAM_BASE      0x0200  // $D200-$D2FF
#define VIDEO_PPU_BASE      0x0300  // $D300-$D304

// PPU registers
#define PPU_CONTROL         0x0300  // $D300
#define PPU_STATUS          0x0301  // $D301
#define PPU_OAM_ADDR        0x0302  // $D302
#define PPU_OAM_DATA        0x0303  // $D303
#define PPU_OAM_DMA         0x0304  // $D304

// Function prototypes
void video_controller_init(void);
void video_controller_process(void);
bool video_controller_handle_read(uint16_t address, uint8_t *data);
bool video_controller_handle_write(uint16_t address, uint8_t data);
void video_controller_prepare_frame_data(void);
bool video_controller_is_frame_ready(void);

#endif // VIDEO_CONTROLLER_H