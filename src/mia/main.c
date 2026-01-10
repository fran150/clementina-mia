#include "pico/stdlib.h"
#include <stdio.h>

#include "sys/mia.h"

int main(void) {
    stdio_init_all();

    sleep_ms(2000);

    mia_init();

    while (true) {        
        tight_loop_contents();
    }
}