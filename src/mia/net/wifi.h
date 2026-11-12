#ifndef _MIA_NET_WIFI_H_
#define _MIA_NET_WIFI_H_

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    MIA_WIFI_MODE_OFF,
    MIA_WIFI_MODE_STA,
    MIA_WIFI_MODE_AP,
} mia_wifi_mode_t;

// Apply the boot settings and connect/start accordingly: WIFI.CFG from the
// SD card when it is present and valid, else the compile-time defaults.
// Called once on boot before mia_init().
bool mia_net_wifi_init(void);

// Connect to an existing Wi-Fi network (STA mode).
// Prints progress and result. Tries up to 3 times, 30 s each with a 2 s pause
// between them (about 95 s at most); a wrong password stops at once.
// Tears down any existing connection first.
bool mia_net_wifi_connect(const char *ssid, const char *password);

// Same as mia_net_wifi_connect / mia_net_wifi_start_ap, with the settings
// read at power-up (WIFI.CFG, else the build). Says why on the console and
// returns false when there are no saved settings for that mode.
bool mia_net_wifi_connect_saved(void);
bool mia_net_wifi_start_ap_saved(void);

// Publish a Wi-Fi access point (AP mode) at 192.168.4.1.
// Tears down any existing connection first.
// Clients must configure a static IP in the 192.168.4.x/24 range.
bool mia_net_wifi_start_ap(const char *ssid, const char *password);

// Disconnect and go idle. The CYW43 chip stays alive (needed for LED).
void mia_net_wifi_off(void);

// Current mode.
mia_wifi_mode_t mia_net_wifi_mode(void);

// Signal strength of the joined network in dBm. False when MIA is not
// joined to a network as a station (off, AP mode or still connecting).
bool mia_net_wifi_rssi(int32_t *rssi);

// Print a human-readable status line to stdout.
void mia_net_wifi_print_status(void);
void mia_net_wifi_print_detail(void);

// Remember/report Wi-Fi setup failures that happen before MIA enters normal mode.
void mia_net_wifi_record_error(uint8_t error);
void mia_net_wifi_report_errors(void);

#endif
