/**
 * Mock hardware/gpio.h for host-based testing
 */

#ifndef HARDWARE_GPIO_MOCK_H
#define HARDWARE_GPIO_MOCK_H

#include "../pico_mock.h"

// GPIO direction
#define GPIO_IN  0
#define GPIO_OUT 1

// Mock GPIO functions
static inline void gpio_init(uint gpio) {
    (void)gpio;
}

static inline void gpio_set_dir(uint gpio, bool out) {
    (void)gpio;
    (void)out;
}

static inline bool gpio_get(uint gpio) {
    (void)gpio;
    return false;  // Mock: always low
}

static inline void gpio_put(uint gpio, bool value) {
    (void)gpio;
    (void)value;
}

static inline void gpio_pull_up(uint gpio) {
    (void)gpio;
}

static inline void gpio_pull_down(uint gpio) {
    (void)gpio;
}

#endif // HARDWARE_GPIO_MOCK_H
