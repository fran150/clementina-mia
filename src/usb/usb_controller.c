/**
 * USB Controller Implementation
 * Dual mode USB support with keyboard buffer management
 */

#include "usb_controller.h"
#include "config/usb_config.h"
#include "tusb.h"
#include "tusb_config.h"

// Keyboard buffer (circular buffer)
static uint8_t keyboard_buffer[USB_KEYBOARD_BUFFER_SIZE];
static uint8_t buffer_head = 0;
static uint8_t buffer_tail = 0;
static bool buffer_full = false;

static usb_mode_t current_mode;

void usb_controller_init(void) {
    // Set USB mode from build-time configuration
#ifdef CONFIG_USB_HOST
    current_mode = USB_MODE_HOST;
#else
    current_mode = USB_MODE_DEVICE;
#endif
    
    // Initialize keyboard buffer
    buffer_head = 0;
    buffer_tail = 0;
    buffer_full = false;
    
    // Initialize TinyUSB based on mode
    if (current_mode == USB_MODE_HOST) {
        // Initialize TinyUSB Host stack
        tusb_init();
    } else {
        // Initialize TinyUSB Device stack
        tusb_init();
    }
}

void usb_controller_process(void) {
    // Process TinyUSB tasks
    tud_task();
    
    // Host mode disabled for now
    #if CFG_TUH_ENABLED
    if (current_mode == USB_MODE_HOST) {
        tuh_task();
    }
    #endif
}

usb_mode_t usb_controller_get_mode(void) {
    return current_mode;
}

bool usb_controller_handle_read(uint16_t address, uint8_t *data) {
    if (!data) return false;
    
    switch (address) {
        case USB_KEYBOARD_DATA:
            return usb_controller_get_key(data);
            
        case USB_KEYBOARD_STATUS: {
            uint8_t status = 0;
            if (usb_controller_is_key_available()) {
                status |= USB_STATUS_KEY_AVAILABLE;
            }
            if (usb_controller_is_buffer_full()) {
                status |= USB_STATUS_BUFFER_FULL;
            }
            *data = status;
            return true;
        }
        
        case USB_BUFFER_HEAD:
            *data = buffer_head;
            return true;
            
        case USB_BUFFER_TAIL:
            *data = buffer_tail;
            return true;
            
        case USB_MODE_STATUS: {
            uint8_t status = 0;
            if (current_mode == USB_MODE_HOST) {
                status |= USB_MODE_HOST;
            }
            // Device connected status would be set by TinyUSB callbacks
            *data = status;
            return true;
        }
        
        default:
            return false;
    }
}

bool usb_controller_handle_write(uint16_t address, uint8_t data) {
    (void)address;  // Unused parameter
    (void)data;     // Unused parameter
    // USB controller typically doesn't handle writes from 6502
    // This could be extended for control registers
    return false;
}

void usb_controller_add_key(uint8_t key_code) {
    if (!buffer_full) {
        keyboard_buffer[buffer_head] = key_code;
        buffer_head = (buffer_head + 1) % USB_KEYBOARD_BUFFER_SIZE;
        
        if (buffer_head == buffer_tail) {
            buffer_full = true;
        }
    }
}

bool usb_controller_get_key(uint8_t *key_code) {
    if (!key_code) return false;
    
    if (buffer_head == buffer_tail && !buffer_full) {
        return false;  // Buffer empty
    }
    
    *key_code = keyboard_buffer[buffer_tail];
    buffer_tail = (buffer_tail + 1) % USB_KEYBOARD_BUFFER_SIZE;
    buffer_full = false;
    
    return true;
}

bool usb_controller_is_buffer_full(void) {
    return buffer_full;
}

bool usb_controller_is_key_available(void) {
    return (buffer_head != buffer_tail) || buffer_full;
}

// TinyUSB callbacks (these will be expanded in later tasks)
void tud_mount_cb(void) {
    // Device mounted callback
}

void tud_umount_cb(void) {
    // Device unmounted callback
}

void tud_suspend_cb(bool remote_wakeup_en) {
    (void)remote_wakeup_en;  // Unused parameter
    // Device suspended callback
}

void tud_resume_cb(void) {
    // Device resumed callback
}