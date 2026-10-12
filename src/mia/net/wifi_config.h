#ifndef _MIA_NET_WIFI_CONFIG_H_
#define _MIA_NET_WIFI_CONFIG_H_

#include <stdbool.h>
#include <stddef.h>

#include "net/wifi.h"

// Wi-Fi settings file read from the root of the SD card at boot.
#define MIA_WIFI_CONFIG_PATH "0:/WIFI.CFG"

// Largest WIFI.CFG MIA accepts, in bytes.
#define MIA_WIFI_CONFIG_MAX_SIZE 512u

#define MIA_WIFI_SSID_MAX_LEN 32u
#define MIA_WIFI_PASSWORD_MIN_LEN 8u
#define MIA_WIFI_PASSWORD_MAX_LEN 63u

typedef struct {
    mia_wifi_mode_t mode;
    char ssid[MIA_WIFI_SSID_MAX_LEN + 1u];
    char password[MIA_WIFI_PASSWORD_MAX_LEN + 1u];
} mia_wifi_config_t;

// Parses the text of WIFI.CFG: one "key = value" per line (keys mode, ssid,
// password), blank lines and lines starting with '#' ignored. Returns false
// for an unusable file and sets *bad_line to the line at fault (0 when the
// problem is the file as a whole) and *reason to a short explanation.
bool mia_wifi_config_parse(const char *text, size_t length, mia_wifi_config_t *out,
                           unsigned *bad_line, const char **reason);

#endif
