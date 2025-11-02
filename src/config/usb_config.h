/**
 * USB Configuration Header
 * Build-time configuration for USB operation mode
 */

#ifndef USB_CONFIG_H
#define USB_CONFIG_H

// USB Mode Configuration
// Uncomment ONE of the following lines to select USB mode:

// #define CONFIG_USB_HOST    // USB Host mode - connect USB hub with keyboard/mouse
#define CONFIG_USB_DEVICE  // USB Device mode - connect to computer for debugging

// Validate configuration
#if defined(CONFIG_USB_HOST) && defined(CONFIG_USB_DEVICE)
#error "Cannot define both CONFIG_USB_HOST and CONFIG_USB_DEVICE"
#endif

#if !defined(CONFIG_USB_HOST) && !defined(CONFIG_USB_DEVICE)
#error "Must define either CONFIG_USB_HOST or CONFIG_USB_DEVICE"
#endif

// USB Host Mode Configuration
#ifdef CONFIG_USB_HOST
#define USB_MAX_DEVICES     8   // Maximum USB devices supported
#define USB_HUB_SUPPORT     1   // Enable USB hub support
#define USB_KEYBOARD_SUPPORT 1  // Enable USB keyboard support
#define USB_MOUSE_SUPPORT   1   // Enable USB mouse support (future)
#endif

// USB Device Mode Configuration
#ifdef CONFIG_USB_DEVICE
#define USB_CDC_SUPPORT     1   // Enable CDC (serial console) support
#define USB_PRINTF_REDIRECT 1   // Redirect printf to USB console
#endif

// Common USB Configuration
#define USB_KEYBOARD_BUFFER_SIZE 16  // Keyboard input buffer size (both modes)

#endif // USB_CONFIG_H