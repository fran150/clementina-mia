#include <stdio.h>
#include "pico/bootrom.h"
#include "pico/stdlib.h"

#include "con.h"

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
