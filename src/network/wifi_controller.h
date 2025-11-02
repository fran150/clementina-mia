/**
 * Wi-Fi Controller System for MIA
 * Manages network communication and video data transmission
 */

#ifndef WIFI_CONTROLLER_H
#define WIFI_CONTROLLER_H

#include <stdint.h>
#include <stdbool.h>

// Network constants
#define WIFI_FRAME_INTERVAL_MS  33  // 30 FPS = 33.33ms per frame
#define WIFI_MAX_CLIENTS        4   // Maximum connected video clients

// Frame data structure sizes
#define FRAME_NAMETABLE_SIZE    1000  // 40x25 bytes
#define FRAME_PALETTE_SIZE      500   // 40x25 * 4 bits (packed)
#define FRAME_OAM_SIZE          1024  // 256 sprites * 4 bytes
#define FRAME_HEADER_SIZE       16    // Frame metadata

typedef enum {
    WIFI_STATE_DISCONNECTED,
    WIFI_STATE_CONNECTING,
    WIFI_STATE_CONNECTED,
    WIFI_STATE_ERROR
} wifi_state_t;

typedef struct {
    uint8_t frame_number;
    uint8_t active_char_table;
    uint16_t frame_size;
    uint32_t timestamp;
    uint8_t reserved[8];
} frame_header_t;

// Function prototypes
void wifi_controller_init(void);
void wifi_controller_process(void);
wifi_state_t wifi_controller_get_state(void);
bool wifi_controller_is_connected(void);
void wifi_controller_transmit_frame(void);
void wifi_controller_transmit_character_table(uint8_t table_index);
void wifi_controller_transmit_palette_bank(uint8_t bank_index);
uint32_t wifi_controller_get_frame_count(void);

#endif // WIFI_CONTROLLER_H