#ifndef _MIA_SYS_LED_H_
#define _MIA_SYS_LED_H_

#include <stdbool.h>

void configure_onboard_led(void);
void turn_onboard_led(bool on);
void update_onboard_led_blink(void);

#endif
