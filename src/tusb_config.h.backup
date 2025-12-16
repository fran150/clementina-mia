/**
 * TinyUSB Configuration for MIA
 * Supports both Host and Device modes
 */

#ifndef TUSB_CONFIG_H
#define TUSB_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

//--------------------------------------------------------------------+
// Board Specific Configuration
//--------------------------------------------------------------------+

// RHPort number used for device can be defined by board.mk, default to port 0
#ifndef BOARD_TUD_RHPORT
#define BOARD_TUD_RHPORT      0
#endif

// RHPort max operational speed can defined by board.mk
#ifndef BOARD_TUD_MAX_SPEED
#define BOARD_TUD_MAX_SPEED   OPT_MODE_DEFAULT_SPEED
#endif

//--------------------------------------------------------------------
// Common Configuration
//--------------------------------------------------------------------

// defined by compiler flags for flexibility
#ifndef CFG_TUSB_MCU
#define CFG_TUSB_MCU          OPT_MCU_RP2040
#endif

#ifndef CFG_TUSB_OS
#define CFG_TUSB_OS           OPT_OS_PICO
#endif

#ifndef CFG_TUSB_DEBUG
#define CFG_TUSB_DEBUG        0
#endif

// Enable Device stack
#define CFG_TUD_ENABLED       1

// Disable Host stack for now (causes conflicts with stdio USB)
#define CFG_TUH_ENABLED       0

// Default is max speed that hardware controller could support with on-chip PHY
#define CFG_TUD_MAX_SPEED     BOARD_TUD_MAX_SPEED

//--------------------------------------------------------------------
// Device Configuration
//--------------------------------------------------------------------

#ifndef CFG_TUD_ENDPOINT0_SIZE
#define CFG_TUD_ENDPOINT0_SIZE    64
#endif

// Device mode configuration
#define CFG_TUSB_RHPORT0_MODE     (OPT_MODE_DEVICE | OPT_MODE_FULL_SPEED)

//------------- CLASS -------------//
#define CFG_TUD_CDC              1
#define CFG_TUD_MSC              0
#define CFG_TUD_HID              0
#define CFG_TUD_MIDI             0
#define CFG_TUD_VENDOR           0

// CDC FIFO size of TX and RX
#define CFG_TUD_CDC_RX_BUFSIZE   (TUD_OPT_HIGH_SPEED ? 512 : 64)
#define CFG_TUD_CDC_TX_BUFSIZE   (TUD_OPT_HIGH_SPEED ? 512 : 64)

// CDC Endpoint transfer buffer size, more is faster
#define CFG_TUD_CDC_EP_BUFSIZE   (TUD_OPT_HIGH_SPEED ? 512 : 64)

//--------------------------------------------------------------------
// Host Configuration (disabled for now)
//--------------------------------------------------------------------

#if CFG_TUH_ENABLED
// Size of buffer to hold descriptors and other data used for enumeration
#define CFG_TUH_ENUMERATION_BUFSIZE 256

#define CFG_TUH_HUB                 1
#define CFG_TUH_CDC                 0
#define CFG_TUH_HID                 2  // Keyboard + Mouse
#define CFG_TUH_MSC                 0
#define CFG_TUH_VENDOR              0

// max device support (excluding hub device)
#define CFG_TUH_DEVICE_MAX          (CFG_TUH_HUB ? 4 : 1) // hub typically has 4 ports

//------------- HID -------------//
#define CFG_TUH_HID_EPIN_BUFSIZE    64
#define CFG_TUH_HID_EPOUT_BUFSIZE   64
#endif

#ifdef __cplusplus
}
#endif

#endif // TUSB_CONFIG_H