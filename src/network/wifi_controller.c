/**
 * Wi-Fi Controller Implementation
 * Network communication and video data transmission
 */

#include "wifi_controller.h"
#include "video/video_controller.h"
#include "pico/cyw43_arch.h"
#include "lwip/udp.h"
#include "pico/time.h"

static wifi_state_t current_state = WIFI_STATE_DISCONNECTED;
static struct udp_pcb *udp_pcb = NULL;
static absolute_time_t last_frame_time;
static uint32_t frame_count = 0;

void wifi_controller_init(void) {
    // Initialize CYW43 Wi-Fi chip
    if (cyw43_arch_init() != 0) {
        current_state = WIFI_STATE_ERROR;
        return;
    }
    
    // Enable station mode
    cyw43_arch_enable_sta_mode();
    
    current_state = WIFI_STATE_DISCONNECTED;
    last_frame_time = get_absolute_time();
    frame_count = 0;
}

void wifi_controller_process(void) {
    // Check if it's time to transmit a new frame
    absolute_time_t current_time = get_absolute_time();
    
    if (absolute_time_diff_us(last_frame_time, current_time) >= (WIFI_FRAME_INTERVAL_MS * 1000)) {
        if (current_state == WIFI_STATE_CONNECTED && video_controller_is_frame_ready()) {
            wifi_controller_transmit_frame();
            last_frame_time = current_time;
        }
    }
    
    // Process network stack
    cyw43_arch_poll();
}

wifi_state_t wifi_controller_get_state(void) {
    return current_state;
}

bool wifi_controller_is_connected(void) {
    return current_state == WIFI_STATE_CONNECTED;
}

void wifi_controller_transmit_frame(void) {
    if (!udp_pcb || current_state != WIFI_STATE_CONNECTED) {
        return;
    }
    
    // Prepare frame header (will be used in later tasks)
    // frame_header_t header;
    // header.frame_number = frame_count & 0xFF;
    // header.active_char_table = 0;  // Will be set by video controller
    // header.frame_size = FRAME_NAMETABLE_SIZE + FRAME_PALETTE_SIZE + FRAME_OAM_SIZE;
    // header.timestamp = to_us_since_boot(get_absolute_time());
    
    // Transmit frame data
    // This will be expanded in later tasks to include actual frame data
    
    frame_count++;
}

void wifi_controller_transmit_character_table(uint8_t table_index) {
    (void)table_index;  // Unused parameter
    if (!udp_pcb || current_state != WIFI_STATE_CONNECTED) {
        return;
    }
    
    // Transmit character table update
    // This will be expanded in later tasks
}

void wifi_controller_transmit_palette_bank(uint8_t bank_index) {
    (void)bank_index;  // Unused parameter
    if (!udp_pcb || current_state != WIFI_STATE_CONNECTED) {
        return;
    }
    
    // Transmit palette bank update
    // This will be expanded in later tasks
}

uint32_t wifi_controller_get_frame_count(void) {
    return frame_count;
}