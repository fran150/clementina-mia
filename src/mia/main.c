#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"

#include <stdio.h>

#include "sys/mia.h"
#include "sys/led.h"
#include "sys/reset.h"
#include "con/con.h"
#include "input/input.h"
#include "net/wifi.h"
#include "sd/sd.h"
#include "video/video.h"

// Runs while an SD request waits on a slow card: the parts of the main loop
// that never touch the card, so Wi-Fi, video frames to the client and input
// keep working. The rest of the loop still waits: a reset request or a speed
// change would reset or reclock the card mid-transfer, and console commands
// can do either.
static void serve_during_sd_wait(void) {
    mia_input_service();
    cyw43_arch_poll();
    mia_video_service();
    update_onboard_led_blink();
}

int main(void) {
    stdio_init_all();
    mia_prepare_reset_lines();

    sleep_ms(2000);

    configure_onboard_led();

    printf("Initializing MIA...\n\n");
    mia_net_wifi_init();
    mia_init();
    mia_sd_set_wait_service(serve_during_sd_wait);

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
