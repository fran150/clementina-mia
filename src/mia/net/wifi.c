#include "wifi.h"

#include <stdio.h>
#include <string.h>

#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"
#include "lwip/netif.h"
#include "lwip/ip4_addr.h"

#include "etc/err.h"
#include "net/wifi_config.h"
#include "sd/sd.h"

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
static uint8_t wifi_error_code;
// Where the boot settings came from: WIFI.CFG on the SD card, or the build.
static const char *config_source = "build";
// The settings read at power-up, kept so a bare 'wifi connect' or 'wifi ap'
// can reuse them without touching the SD card again. Never printed whole:
// the password stays in RAM only.
static mia_wifi_config_t saved_config;
static bool saved_config_valid;

// Boot joins and console joins try this many times before giving up, with a
// short pause and a fresh station mode between attempts. A first join right
// after power-up often fails with a link or timeout error while the radio
// and the access point settle; a later attempt usually works.
#define MIA_WIFI_CONNECT_ATTEMPTS   3
#define MIA_WIFI_CONNECT_TIMEOUT_MS 30000
#define MIA_WIFI_RETRY_PAUSE_MS     2000

static bool wifi_arch_available(void) {
    if (wifi_error_code == ERROR_WIFI_INIT_FAILED) {
        printf("Wi-Fi: CYW43 is unavailable.\n");
        error_push(ERROR_WIFI_INIT_FAILED);
        return false;
    }

    return true;
}

static uint32_t sta_auth(const char *password) {
    return strlen(password) == 0 ? CYW43_AUTH_OPEN : CYW43_AUTH_WPA2_AES_PSK;
}

bool mia_net_wifi_connect(const char *ssid, const char *password) {
    if (!wifi_arch_available()) {
        return false;
    }

    cyw43_arch_enable_sta_mode();
    current_mode = MIA_WIFI_MODE_OFF;

    printf("Wi-Fi: connecting to '%s'...\n", ssid);
    int rc = 0;
    for (int attempt = 1; attempt <= MIA_WIFI_CONNECT_ATTEMPTS; attempt++) {
        if (attempt > 1) {
            // Drop the half-made link and start the station again.
            cyw43_arch_disable_sta_mode();
            sleep_ms(MIA_WIFI_RETRY_PAUSE_MS);
            cyw43_arch_enable_sta_mode();
            printf("Wi-Fi: attempt %d of %d...\n", attempt, MIA_WIFI_CONNECT_ATTEMPTS);
        }

        rc = cyw43_arch_wifi_connect_timeout_ms(ssid, password, sta_auth(password),
                                                MIA_WIFI_CONNECT_TIMEOUT_MS);
        if (rc == 0) {
            break;
        }

        printf("Wi-Fi: connection failed (%d)\n", rc);
        if (rc == PICO_ERROR_BADAUTH) {
            // Another attempt cannot fix a wrong password.
            printf("Wi-Fi: wrong password; not retrying.\n");
            break;
        }
    }

    if (rc != 0) {
        wifi_error_code = ERROR_WIFI_CONNECT_FAILED;
        error_push(ERROR_WIFI_CONNECT_FAILED);
        return false;
    }

    strncpy(current_ssid, ssid, sizeof(current_ssid) - 1);
    current_ssid[sizeof(current_ssid) - 1] = '\0';
    current_mode = MIA_WIFI_MODE_STA;
    wifi_error_code = 0;

    if (netif_default != NULL) {
        printf("Wi-Fi: connected at %s\n", ip4addr_ntoa(netif_ip4_addr(netif_default)));
    } else {
        printf("Wi-Fi: connected\n");
    }
    return true;
}

bool mia_net_wifi_start_ap(const char *ssid, const char *password) {
    if (!wifi_arch_available()) {
        return false;
    }

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
    wifi_error_code = 0;

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
    if (wifi_error_code != ERROR_WIFI_INIT_FAILED) {
        wifi_error_code = 0;
    }
    printf("Wi-Fi: off\n");
}

mia_wifi_mode_t mia_net_wifi_mode(void) {
    return current_mode;
}

static const char *wifi_mode_name(mia_wifi_mode_t mode) {
    switch (mode) {
        case MIA_WIFI_MODE_OFF:
            return "off";
        case MIA_WIFI_MODE_STA:
            return "sta";
        case MIA_WIFI_MODE_AP:
            return "ap";
        default:
            return "unknown";
    }
}

bool mia_net_wifi_rssi(int32_t *rssi) {
    if (current_mode != MIA_WIFI_MODE_STA ||
        cyw43_wifi_link_status(&cyw43_state, CYW43_ITF_STA) != CYW43_LINK_JOIN) {
        return false;
    }
    return cyw43_wifi_get_rssi(&cyw43_state, rssi) == 0;
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

void mia_net_wifi_print_detail(void) {
    printf("Wi-Fi:\n");
    printf("  mode:       %s\n", wifi_mode_name(current_mode));
    printf("  config:     %s\n", config_source);
    printf("  ssid:       %s\n", current_ssid[0] ? current_ssid : "(none)");
    printf("  last-error: 0x%02X\n", (unsigned)wifi_error_code);

    if (netif_default == NULL) {
        printf("  netif:      unavailable\n");
        return;
    }

    printf("  netif:      up:%s  link:%s\n",
           netif_is_up(netif_default) ? "yes" : "no",
           netif_is_link_up(netif_default) ? "yes" : "no");
    printf("  ip:         %s\n", ip4addr_ntoa(netif_ip4_addr(netif_default)));
    printf("  netmask:    %s\n", ip4addr_ntoa(netif_ip4_netmask(netif_default)));
    printf("  gateway:    %s\n", ip4addr_ntoa(netif_ip4_gw(netif_default)));

    if (current_mode == MIA_WIFI_MODE_AP) {
        printf("  AP clients: static IP in %d.%d.%d.x/24\n",
               MIA_NET_AP_IP_A, MIA_NET_AP_IP_B, MIA_NET_AP_IP_C);
    }
}

void mia_net_wifi_record_error(uint8_t error) {
    wifi_error_code = error;
}

void mia_net_wifi_report_errors(void) {
    if (wifi_error_code != 0) {
        error_push(wifi_error_code);
    }
}

// Reads and checks WIFI.CFG. Returns false, after saying why on the console,
// when the build settings should be used instead. Never prints the password.
static bool wifi_load_sd_config(mia_wifi_config_t *config) {
    static char text[MIA_WIFI_CONFIG_MAX_SIZE];
    size_t length = 0;
    bool ok = false;

    switch (mia_sd_read_boot_file(MIA_WIFI_CONFIG_PATH, (uint8_t *)text, sizeof(text), &length)) {
        case MIA_SD_BOOT_FILE_OK: {
            unsigned bad_line = 0;
            const char *reason = "";
            ok = mia_wifi_config_parse(text, length, config, &bad_line, &reason);
            if (!ok && bad_line != 0) {
                printf("Wi-Fi: WIFI.CFG line %u: %s; using build settings.\n", bad_line, reason);
            } else if (!ok) {
                printf("Wi-Fi: WIFI.CFG: %s; using build settings.\n", reason);
            }
            break;
        }
        case MIA_SD_BOOT_FILE_NO_CARD:
            printf("Wi-Fi: no readable SD card; using build settings.\n");
            break;
        case MIA_SD_BOOT_FILE_MISSING:
            printf("Wi-Fi: no WIFI.CFG on the SD card; using build settings.\n");
            break;
        case MIA_SD_BOOT_FILE_TOO_LARGE:
            printf("Wi-Fi: WIFI.CFG is over %u bytes; using build settings.\n",
                   (unsigned)MIA_WIFI_CONFIG_MAX_SIZE);
            break;
        case MIA_SD_BOOT_FILE_READ_ERROR:
            printf("Wi-Fi: could not read WIFI.CFG; using build settings.\n");
            break;
    }

    memset(text, 0, sizeof(text));
    return ok;
}

static bool wifi_init_from_sd(void) {
    mia_wifi_config_t config;
    if (!wifi_load_sd_config(&config)) {
        return false;
    }

    config_source = "WIFI.CFG";
    saved_config = config;
    saved_config_valid = true;
    printf("Wi-Fi: using WIFI.CFG from the SD card.\n");
    switch (config.mode) {
        case MIA_WIFI_MODE_STA:
            mia_net_wifi_connect(config.ssid, config.password);
            break;
        case MIA_WIFI_MODE_AP:
            mia_net_wifi_start_ap(config.ssid, config.password);
            break;
        default:
            mia_net_wifi_off();
            break;
    }
    memset(&config, 0, sizeof(config));
    return true;
}

// Keeps the build settings for a bare 'wifi connect' or 'wifi ap' when the
// card gave none.
static void wifi_save_build_config(void) {
#ifdef MIA_WIFI_SSID
    if (MIA_WIFI_SSID[0] == '\0' ||
        strlen(MIA_WIFI_SSID) >= sizeof(saved_config.ssid) ||
        strlen(MIA_WIFI_PASSWORD) >= sizeof(saved_config.password)) {
        return;
    }
    saved_config.mode = MIA_WIFI_DEFAULT_MODE == MIA_WIFI_MODE_AP ? MIA_WIFI_MODE_AP
                                                                  : MIA_WIFI_MODE_STA;
    strcpy(saved_config.ssid, MIA_WIFI_SSID);
    strcpy(saved_config.password, MIA_WIFI_PASSWORD);
    saved_config_valid = true;
#endif
}

static bool wifi_init_from_build(void) {
    wifi_save_build_config();
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
    printf("Wi-Fi: no default configured (add WIFI.CFG to the SD card, or build with MIA_WIFI_MODE and MIA_WIFI_SSID).\n");
    return false;
#endif
}

bool mia_net_wifi_init(void) {
    if (wifi_init_from_sd()) {
        return current_mode != MIA_WIFI_MODE_OFF;
    }
    return wifi_init_from_build();
}

// The saved settings name a network to join when they are for station mode,
// or for mode = off with an SSID (Wi-Fi stays off at boot, but the network
// is known), and an access point to publish when they are for AP mode.
bool mia_net_wifi_connect_saved(void) {
    if (!saved_config_valid || saved_config.ssid[0] == '\0') {
        printf("Wi-Fi: no saved network (none in WIFI.CFG at power-up or in the build).\n");
        printf("Usage: wifi connect <ssid> [password]\n");
        return false;
    }
    if (saved_config.mode == MIA_WIFI_MODE_AP) {
        printf("Wi-Fi: the saved settings (%s) are for AP mode; use 'wifi ap'.\n", config_source);
        return false;
    }

    printf("Wi-Fi: using the saved settings (%s).\n", config_source);
    return mia_net_wifi_connect(saved_config.ssid, saved_config.password);
}

bool mia_net_wifi_start_ap_saved(void) {
    if (!saved_config_valid || saved_config.mode != MIA_WIFI_MODE_AP) {
        printf("Wi-Fi: no saved AP settings (WIFI.CFG or the build must use mode = ap).\n");
        printf("Usage: wifi ap <ssid> [password]\n");
        return false;
    }

    printf("Wi-Fi: using the saved settings (%s).\n", config_source);
    return mia_net_wifi_start_ap(saved_config.ssid, saved_config.password);
}
