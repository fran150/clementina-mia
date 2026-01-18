#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "pico/bootrom.h"

#include <stdio.h>

#include "sys/mia.h"
#include "hardware/gpio_mapping.h"

void configure_debug_leds() {
    printf("Debug led mode initialized...\n");
    // Initialize GPIOs 8–15 as outputs
    for (int pin = MIA_DATA_PIN_BASE; pin < MIA_DATA_PIN_BASE + 8; pin++) {
        gpio_init(pin);
        gpio_set_dir(pin, GPIO_OUT);
        gpio_put(pin, 0); // start OFF
    }
}

void configure_onboard_led() {
    printf("Onboard led initialized...\n");
    cyw43_arch_init();
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
}

void turn_debug_leds(bool on) {
    for (int pin = MIA_DATA_PIN_BASE; pin < MIA_DATA_PIN_BASE + 8; pin++) {
        gpio_put(pin, on);
    }
}

void turn_onboard_led(bool on) {
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, on);
}

char read_character_from_console() {
    int c = getchar_timeout_us(0);
    if (c != PICO_ERROR_TIMEOUT) {
        return (char)c;
    }

    return '\0';
}

void eval_reboot_to_bootsel(char option) {
    if (option == 'q') {
        printf("Rebooting to BOOTSEL...\n");
        reset_usb_boot(0, 0);
    }
}

void init_debug_input(void) {
    for (int pin = MIA_ADDR_PIN_BASE; pin < MIA_ADDR_PIN_BASE + 5; pin++) {
        gpio_init(pin);
        gpio_set_dir(pin, GPIO_IN);
        // Optional: disable pulls by default
        gpio_disable_pulls(pin);
    }

    gpio_init(MIA_CS_PIN);
    gpio_set_dir(MIA_CS_PIN, GPIO_IN);
    gpio_disable_pulls(MIA_CS_PIN);

    gpio_init(MIA_RWB_PIN);
    gpio_set_dir(MIA_RWB_PIN, GPIO_IN);
    gpio_disable_pulls(MIA_RWB_PIN);
}

void scan_address_bus_and_lines(void) {
    uint32_t gpio_state = gpio_get_all();

    // Extract GPIO 16–20 into bits 0–4
    uint8_t address_value = (gpio_state >> 16) & 0x1F;

    printf("---\n");
    printf("CS line enabled: %ld\n", ((gpio_state >> MIA_CS_PIN) & 1));
    printf("R/W line enabled: %i\n", !((gpio_state >> MIA_RWB_PIN) & 1));
    printf("Address bus is: %02X\n", address_value);
    printf("---\n");
}

int main(void) {
    stdio_init_all();

    sleep_ms(2000);

    configure_onboard_led();

    configure_debug_leds();
    init_debug_input();

    char option = read_character_from_console();

    while (option != 'c') {        
        turn_onboard_led(1);
        turn_debug_leds(1);
        sleep_ms(500);

        turn_onboard_led(0);
        turn_debug_leds(0);
        sleep_ms(500);

        scan_address_bus_and_lines();

        option = read_character_from_console();
        eval_reboot_to_bootsel(option);
    }

    printf("Initializing MIA...\n\n");
    mia_init();

    while (true) {
        turn_onboard_led(1);
        sleep_ms(500);

        turn_onboard_led(0);
        sleep_ms(500);

        option = read_character_from_console();
        eval_reboot_to_bootsel(option);

        tight_loop_contents();
    }
}
