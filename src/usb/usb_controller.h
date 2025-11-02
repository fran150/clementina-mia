/**
 * USB Controller System for MIA
 * Manages dual USB mode operation (Host/Device)
 */

#ifndef USB_CONTROLLER_H
#define USB_CONTROLLER_H

#include <stdint.h>
#include <stdbool.h>
#include "config/usb_config.h"

// Memory-mapped I/O addresses (relative to general interface base)
#define USB_KEYBOARD_DATA   0x0000  // $C000
#define USB_KEYBOARD_STATUS 0x0001  // $C001
#define USB_BUFFER_HEAD     0x0002  // $C002
#define USB_BUFFER_TAIL     0x0003  // $C003
#define USB_MODE_STATUS     0x0004  // $C004

// Status register bits
#define USB_STATUS_KEY_AVAILABLE    0x01
#define USB_STATUS_BUFFER_FULL      0x80

// Mode status bits
#define USB_STATUS_MODE_HOST        0x01
#define USB_STATUS_DEVICE_CONNECTED 0x02

typedef enum {
    USB_MODE_DEVICE,
    USB_MODE_HOST
} usb_mode_t;

// Function prototypes
void usb_controller_init(void);
void usb_controller_process(void);
usb_mode_t usb_controller_get_mode(void);
bool usb_controller_handle_read(uint16_t address, uint8_t *data);
bool usb_controller_handle_write(uint16_t address, uint8_t data);
void usb_controller_add_key(uint8_t key_code);
bool usb_controller_get_key(uint8_t *key_code);
bool usb_controller_is_buffer_full(void);
bool usb_controller_is_key_available(void);

#endif // USB_CONTROLLER_H