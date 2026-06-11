#ifndef _MIA_NET_WIFI_H_
#define _MIA_NET_WIFI_H_

#include <stdbool.h>

typedef enum {
    MIA_WIFI_MODE_OFF,
    MIA_WIFI_MODE_STA,
    MIA_WIFI_MODE_AP,
} mia_wifi_mode_t;

// Apply compile-time defaults and connect/start accordingly.
// Called once on boot before mia_init().
bool mia_net_wifi_init(void);

// Connect to an existing Wi-Fi network (STA mode).
// Prints progress and result. Blocks up to 30 s.
// Tears down any existing connection first.
bool mia_net_wifi_connect(const char *ssid, const char *password);

// Publish a Wi-Fi access point (AP mode) at 192.168.4.1.
// Tears down any existing connection first.
// Clients must configure a static IP in the 192.168.4.x/24 range.
bool mia_net_wifi_start_ap(const char *ssid, const char *password);

// Disconnect and go idle. The CYW43 chip stays alive (needed for LED).
void mia_net_wifi_off(void);

// Current mode.
mia_wifi_mode_t mia_net_wifi_mode(void);

// Print a human-readable status line to stdout.
void mia_net_wifi_print_status(void);

#endif
