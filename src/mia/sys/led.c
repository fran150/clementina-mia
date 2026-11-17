#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"

#include "etc/err.h"
#include "led.h"
#include "net/wifi.h"

static bool onboard_led_ready;

// The onboard LED is wired to the CYW43 Wi-Fi chip, so every change is a
// blocking request over the Wi-Fi link. It is switched on once at boot as a
// "MIA is up" indicator and never blinked: a blink from the main loop costs
// two link requests a second, and each one stalls the loop for up to 500 ms
// when the chip is slow to answer. Use the console for live status.
void configure_onboard_led(void) {
    printf("Onboard led initialized...\n");
    int rc = cyw43_arch_init();
    if (rc != 0) {
        printf("CYW43 init failed (%d)\n", rc);
        mia_net_wifi_record_error(ERROR_WIFI_INIT_FAILED);
        return;
    }
    onboard_led_ready = true;
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
}

void turn_onboard_led(bool on) {
    if (!onboard_led_ready) {
        return;
    }

    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, on);
}
