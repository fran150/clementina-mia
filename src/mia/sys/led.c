#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"

#include "led.h"

void configure_onboard_led(void) {
    printf("Onboard led initialized...\n");
    cyw43_arch_init();
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
}

void turn_onboard_led(bool on) {
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, on);
}

void update_onboard_led_blink(void) {
    const uint32_t blink_interval_us = 500 * 1000;
    static uint32_t last_toggle_us = 0;
    static bool led_on = false;

    uint32_t now_us = time_us_32();
    if ((uint32_t)(now_us - last_toggle_us) < blink_interval_us) {
        return;
    }

    led_on = !led_on;
    turn_onboard_led(led_on);
    last_toggle_us = now_us;
}
