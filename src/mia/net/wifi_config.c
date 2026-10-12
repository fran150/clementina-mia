#include "net/wifi_config.h"

#include <ctype.h>
#include <string.h>

static bool is_blank(char c) {
    return c == ' ' || c == '\t' || c == '\r';
}

// Trims [*start, *end) of surrounding blanks.
static void trim(const char **start, const char **end) {
    while (*start < *end && is_blank(**start)) {
        (*start)++;
    }
    while (*end > *start && is_blank((*end)[-1])) {
        (*end)--;
    }
}

static bool key_is(const char *key, size_t key_len, const char *name) {
    if (strlen(name) != key_len) {
        return false;
    }
    for (size_t i = 0; i < key_len; i++) {
        if (tolower((unsigned char)key[i]) != name[i]) {
            return false;
        }
    }
    return true;
}

static bool copy_value(char *dest, size_t dest_size, const char *value, size_t value_len) {
    if (value_len >= dest_size) {
        return false;
    }
    memcpy(dest, value, value_len);
    dest[value_len] = '\0';
    return true;
}

static bool fail(unsigned line, const char *why, unsigned *bad_line, const char **reason) {
    *bad_line = line;
    *reason = why;
    return false;
}

bool mia_wifi_config_parse(const char *text, size_t length, mia_wifi_config_t *out,
                           unsigned *bad_line, const char **reason) {
    memset(out, 0, sizeof(*out));
    out->mode = MIA_WIFI_MODE_STA;
    bool has_mode = false;
    bool has_ssid = false;

    const char *pos = text;
    const char *text_end = text + length;
    unsigned line = 0;

    // Skip the UTF-8 byte order mark some editors write at the start.
    if (length >= 3u && memcmp(text, "\xEF\xBB\xBF", 3u) == 0) {
        pos += 3;
    }

    while (pos < text_end) {
        line++;
        const char *line_end = memchr(pos, '\n', (size_t)(text_end - pos));
        if (line_end == NULL) {
            line_end = text_end;
        }

        const char *start = pos;
        const char *end = line_end;
        pos = line_end < text_end ? line_end + 1 : text_end;

        trim(&start, &end);
        if (start == end || *start == '#') {
            continue;
        }

        const char *equals = memchr(start, '=', (size_t)(end - start));
        if (equals == NULL) {
            return fail(line, "expected key = value", bad_line, reason);
        }

        const char *key = start;
        const char *key_end = equals;
        const char *value = equals + 1;
        const char *value_end = end;
        trim(&key, &key_end);
        trim(&value, &value_end);
        size_t key_len = (size_t)(key_end - key);
        size_t value_len = (size_t)(value_end - value);

        if (key_is(key, key_len, "mode")) {
            if (key_is(value, value_len, "sta")) {
                out->mode = MIA_WIFI_MODE_STA;
            } else if (key_is(value, value_len, "ap")) {
                out->mode = MIA_WIFI_MODE_AP;
            } else if (key_is(value, value_len, "off")) {
                out->mode = MIA_WIFI_MODE_OFF;
            } else {
                return fail(line, "mode must be sta, ap or off", bad_line, reason);
            }
            has_mode = true;
        } else if (key_is(key, key_len, "ssid")) {
            if (value_len == 0 || !copy_value(out->ssid, sizeof(out->ssid), value, value_len)) {
                return fail(line, "ssid must be 1-32 characters", bad_line, reason);
            }
            has_ssid = true;
        } else if (key_is(key, key_len, "password")) {
            if ((value_len != 0 && value_len < MIA_WIFI_PASSWORD_MIN_LEN) ||
                !copy_value(out->password, sizeof(out->password), value, value_len)) {
                return fail(line, "password must be empty or 8-63 characters", bad_line, reason);
            }
        } else {
            return fail(line, "unknown key (use mode, ssid or password)", bad_line, reason);
        }
    }

    if (!has_mode && !has_ssid) {
        return fail(0, "no settings", bad_line, reason);
    }
    if (out->mode != MIA_WIFI_MODE_OFF && !has_ssid) {
        return fail(0, "ssid is missing", bad_line, reason);
    }
    return true;
}
