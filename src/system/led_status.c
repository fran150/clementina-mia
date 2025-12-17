/**
 * LED Status Indicator System Implementation
 */

#include "led_status.h"
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "hardware/gpio.h"
#include "hardware/gpio_mapping.h"

static led_status_t current_status = LED_STATUS_OFF;
static uint32_t last_update = 0;
static uint8_t blink_count = 0;
static bool led_state = false;
static bool cyw43_available = false;

void led_status_init(void) {
    // Try to initialize CYW43 for LED control
    if (cyw43_arch_init() == 0) {
        cyw43_available = true;
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
    } else {
        // Fallback to GPIO 25 for regular Pico
        cyw43_available = false;
        gpio_init(GPIO_LED);
        gpio_set_dir(GPIO_LED, GPIO_OUT);
        gpio_put(GPIO_LED, 0);
    }
    
    current_status = LED_STATUS_INIT;
    led_state = true;
    
    // Turn on LED immediately to show initialization
    if (cyw43_available) {
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
    } else {
        gpio_put(25, 1);
    }
}

void led_status_set(led_status_t status) {
    current_status = status;
    blink_count = 0;
    last_update = to_ms_since_boot(get_absolute_time());
}

static void set_led(bool state) {
    led_state = state;
    if (cyw43_available) {
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, state);
    } else {
        gpio_put(GPIO_LED, state);
    }
}

void led_status_update(void) {
    uint32_t now = to_ms_since_boot(get_absolute_time());
    
    switch (current_status) {
        case LED_STATUS_INIT:
            // Solid on
            set_led(true);
            break;
            
        case LED_STATUS_USB_READY:
            // 2 quick blinks then off
            if (now - last_update > 150) {
                if (blink_count < 4) { // 2 blinks = 4 state changes
                    set_led(!led_state);
                    blink_count++;
                } else {
                    set_led(false);
                }
                last_update = now;
            }
            break;
            
        case LED_STATUS_BOOT_COMPLETE:
            // 3 quick blinks then off
            if (now - last_update > 150) {
                if (blink_count < 6) { // 3 blinks = 6 state changes
                    set_led(!led_state);
                    blink_count++;
                } else {
                    set_led(false);
                }
                last_update = now;
            }
            break;
            
        case LED_STATUS_RUNNING:
            // Slow blink (1 second on/off)
            if (now - last_update > 1000) {
                set_led(!led_state);
                last_update = now;
            }
            break;
            
        case LED_STATUS_ERROR:
            // Fast blink (200ms on/off)
            if (now - last_update > 200) {
                set_led(!led_state);
                last_update = now;
            }
            break;
            
        case LED_STATUS_OFF:
        default:
            set_led(false);
            break;
    }
}
