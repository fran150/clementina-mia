#include "wifi.h"

#include <stdio.h>
#include <string.h>

#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"
#include "lwip/netif.h"
#include "lwip/ip4_addr.h"

// Compile-time default mode (set by CMakeLists.txt from MIA_WIFI_MODE env var).
// 0 = off, 1 = sta, 2 = ap
#ifndef MIA_WIFI_DEFAULT_MODE
#define MIA_WIFI_DEFAULT_MODE MIA_WIFI_MODE_OFF
#endif

#ifndef MIA_WIFI_PASSWORD
#define MIA_WIFI_PASSWORD ""
#endif

// 192.168.4.1/24 is the conventional AP address for embedded Wi-Fi devices.
#define MIA_NET_AP_IP_A 192
#define MIA_NET_AP_IP_B 168
#define MIA_NET_AP_IP_C   4
#define MIA_NET_AP_IP_D   1

static mia_wifi_mode_t current_mode = MIA_WIFI_MODE_OFF;
static char current_ssid[64];

static uint32_t sta_auth(const char *password) {
    return strlen(password) == 0 ? CYW43_AUTH_OPEN : CYW43_AUTH_WPA2_AES_PSK;
}

bool mia_net_wifi_connect(const char *ssid, const char *password) {
    cyw43_arch_enable_sta_mode();
    current_mode = MIA_WIFI_MODE_OFF;

    printf("Wi-Fi: connecting to '%s'...\n", ssid);
    int rc = cyw43_arch_wifi_connect_timeout_ms(ssid, password, sta_auth(password), 30000);
    if (rc != 0) {
        printf("Wi-Fi: connection failed (%d)\n", rc);
        return false;
    }

    strncpy(current_ssid, ssid, sizeof(current_ssid) - 1);
    current_ssid[sizeof(current_ssid) - 1] = '\0';
    current_mode = MIA_WIFI_MODE_STA;

    if (netif_default != NULL) {
        printf("Wi-Fi: connected at %s\n", ip4addr_ntoa(netif_ip4_addr(netif_default)));
    } else {
        printf("Wi-Fi: connected\n");
    }
    return true;
}

bool mia_net_wifi_start_ap(const char *ssid, const char *password) {
    uint32_t auth = sta_auth(password);
    cyw43_arch_enable_ap_mode(ssid, password, auth);

    ip4_addr_t ip, mask, gw;
    IP4_ADDR(&ip,   MIA_NET_AP_IP_A, MIA_NET_AP_IP_B, MIA_NET_AP_IP_C, MIA_NET_AP_IP_D);
    IP4_ADDR(&mask, 255, 255, 255, 0);
    IP4_ADDR(&gw,   MIA_NET_AP_IP_A, MIA_NET_AP_IP_B, MIA_NET_AP_IP_C, MIA_NET_AP_IP_D);
    netif_set_addr(netif_default, &ip, &mask, &gw);

    strncpy(current_ssid, ssid, sizeof(current_ssid) - 1);
    current_ssid[sizeof(current_ssid) - 1] = '\0';
    current_mode = MIA_WIFI_MODE_AP;

    printf("Wi-Fi: AP '%s' active at %d.%d.%d.%d\n",
           ssid,
           MIA_NET_AP_IP_A, MIA_NET_AP_IP_B, MIA_NET_AP_IP_C, MIA_NET_AP_IP_D);
    printf("Wi-Fi: clients must use a static IP in %d.%d.%d.x/24\n",
           MIA_NET_AP_IP_A, MIA_NET_AP_IP_B, MIA_NET_AP_IP_C);
    return true;
}

void mia_net_wifi_off(void) {
    if (current_mode == MIA_WIFI_MODE_STA) {
        cyw43_arch_disable_sta_mode();
    }
    current_mode = MIA_WIFI_MODE_OFF;
    current_ssid[0] = '\0';
    printf("Wi-Fi: off\n");
}

mia_wifi_mode_t mia_net_wifi_mode(void) {
    return current_mode;
}

void mia_net_wifi_print_status(void) {
    switch (current_mode) {
        case MIA_WIFI_MODE_OFF:
            printf("Wi-Fi: off\n");
            break;
        case MIA_WIFI_MODE_STA:
            if (netif_default != NULL) {
                printf("Wi-Fi: STA  SSID: %s  IP: %s\n",
                       current_ssid, ip4addr_ntoa(netif_ip4_addr(netif_default)));
            } else {
                printf("Wi-Fi: STA  SSID: %s  (no IP)\n", current_ssid);
            }
            break;
        case MIA_WIFI_MODE_AP:
            printf("Wi-Fi: AP   SSID: %s  IP: %d.%d.%d.%d\n",
                   current_ssid,
                   MIA_NET_AP_IP_A, MIA_NET_AP_IP_B, MIA_NET_AP_IP_C, MIA_NET_AP_IP_D);
            break;
    }
}

bool mia_net_wifi_init(void) {
#if MIA_WIFI_DEFAULT_MODE == MIA_WIFI_MODE_AP
#ifdef MIA_WIFI_SSID
    if (MIA_WIFI_SSID[0] != '\0') {
        return mia_net_wifi_start_ap(MIA_WIFI_SSID, MIA_WIFI_PASSWORD);
    }
    printf("Wi-Fi: AP mode requested but MIA_WIFI_SSID is empty; staying off.\n");
#else
    printf("Wi-Fi: AP mode requested but MIA_WIFI_SSID not set at build time.\n");
#endif
    return false;

#elif MIA_WIFI_DEFAULT_MODE == MIA_WIFI_MODE_STA
#ifdef MIA_WIFI_SSID
    if (MIA_WIFI_SSID[0] != '\0') {
        return mia_net_wifi_connect(MIA_WIFI_SSID, MIA_WIFI_PASSWORD);
    }
    printf("Wi-Fi: STA mode requested but MIA_WIFI_SSID is empty; staying off.\n");
#else
    printf("Wi-Fi: set MIA_WIFI_SSID and MIA_WIFI_PASSWORD at build time to auto-connect.\n");
#endif
    return false;

#else
    printf("Wi-Fi: no default configured (build with MIA_WIFI_MODE and MIA_WIFI_SSID to auto-connect).\n");
    return false;
#endif
}
