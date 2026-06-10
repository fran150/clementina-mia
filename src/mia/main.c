#include "pico/stdlib.h"

#include <stdio.h>

#include "pico/cyw43_arch.h"

#include "sys/mia.h"
#include "mia/misc/led.h"
#include "mia/misc/con.h"
#include "sys/reset.h"
#include "video/video.h"


int main(void) {
    stdio_init_all();
    mia_prepare_reset_lines();

    sleep_ms(2000);

    configure_onboard_led();

    char option = read_character_from_console();

    printf("Initializing MIA...\n\n");
    mia_video_wifi_init();
    mia_init();

    while (true) {
        mia_handle_reset_request();
        mia_service();
        cyw43_arch_poll();
        mia_video_service();
        update_onboard_led_blink();

        option = read_character_from_console();
        eval_reboot_to_bootsel(option);

        tight_loop_contents();
    }
}
