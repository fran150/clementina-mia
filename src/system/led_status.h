/**
 * LED Status Indicator System
 * Provides visual feedback for system status using the onboard LED
 */

#ifndef LED_STATUS_H
#define LED_STATUS_H

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    LED_STATUS_INIT,           // Solid on - System initializing
    LED_STATUS_USB_READY,      // 2 quick blinks - USB ready
    LED_STATUS_BOOT_COMPLETE,  // 3 quick blinks - Boot sequence complete
    LED_STATUS_RUNNING,        // Slow blink - Normal operation
    LED_STATUS_ERROR,          // Fast blink - Error state
    LED_STATUS_OFF             // LED off
} led_status_t;

/**
 * Initialize the LED status system
 */
void led_status_init(void);

/**
 * Set the LED status pattern
 */
void led_status_set(led_status_t status);

/**
 * Update LED status (call regularly from main loop)
 */
void led_status_update(void);

#endif // LED_STATUS_H
