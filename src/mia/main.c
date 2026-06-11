#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"

#include <stdio.h>

#include "sys/mia.h"
#include "sys/led.h"
#include "sys/reset.h"
#include "con/con.h"
#include "net/wifi.h"
#include "video/video.h"


int main(void) {
    stdio_init_all();
    mia_prepare_reset_lines();

    sleep_ms(2000);

    configure_onboard_led();

    printf("Initializing MIA...\n\n");
    mia_net_wifi_init();
    mia_init();

    printf("MIA ready. Type 'help' for commands.\n");

    while (true) {
        mia_handle_reset_request();
        mia_service();
        cyw43_arch_poll();
        mia_video_service();
        update_onboard_led_blink();

        con_process();

        tight_loop_contents();
    }
}
