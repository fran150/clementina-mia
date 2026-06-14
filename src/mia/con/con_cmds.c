#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#include "pico/bootrom.h"

#include "etc/err.h"
#include "etc/status.h"
#include "mem/regs.h"
#include "mem/mem.h"
#include "input/input.h"
#include "sys/speed.h"
#include "net/wifi.h"

// Defined in con.c — switches the engine into monitor or input mode.
extern void con_enter_monitor(void);
extern void con_enter_input(void);

// ---- Helpers ---------------------------------------------------------------

static const char *skip_ws(const char *p) {
    while (*p == ' ' || *p == '\t') p++;
    return p;
}

static const char *error_name(uint8_t code) {
    switch (code) {
        case ERROR_MIA_CANNOT_ALLOCATE_RAM: return "ERROR_MIA_CANNOT_ALLOCATE_RAM";
        case ERROR_QUEUE_OVERFLOW: return "ERROR_QUEUE_OVERFLOW";
        case ERROR_DMA_SIZE_ZERO: return "ERROR_DMA_SIZE_ZERO";
        case ERROR_DMA_SRC_WILL_OVERFLOW: return "ERROR_DMA_SRC_WILL_OVERFLOW";
        case ERROR_DMA_TGT_WILL_OVERFLOW: return "ERROR_DMA_TGT_WILL_OVERFLOW";
        case ERROR_CMD_QUEUE_FULL: return "ERROR_CMD_QUEUE_FULL";
        case ERROR_CMD_UNKNOWN: return "ERROR_CMD_UNKNOWN";
        case ERROR_WIFI_INIT_FAILED: return "ERROR_WIFI_INIT_FAILED";
        case ERROR_WIFI_CONNECT_FAILED: return "ERROR_WIFI_CONNECT_FAILED";
        case ERROR_VIDEO_UDP_ALLOC_FAILED: return "ERROR_VIDEO_UDP_ALLOC_FAILED";
        case ERROR_VIDEO_UDP_BIND_FAILED: return "ERROR_VIDEO_UDP_BIND_FAILED";
        case ERROR_INPUT_MODE_UNAVAILABLE: return "ERROR_INPUT_MODE_UNAVAILABLE";
        case ERROR_INPUT_PROBE_INVALID: return "ERROR_INPUT_PROBE_INVALID";
        case ERROR_INPUT_UDP_ALLOC_FAILED: return "ERROR_INPUT_UDP_ALLOC_FAILED";
        case ERROR_INPUT_UDP_BIND_FAILED: return "ERROR_INPUT_UDP_BIND_FAILED";
        default: return "UNKNOWN_ERROR";
    }
}

// ---- Command handlers -------------------------------------------------------

static void cmd_status(const char *args) {
    (void)args;
    uint16_t st = mia_regs->mia_status;

    printf("MIA Status:\n");
    printf("  Mode:   %s\n", (st & MIA_STAT_MASTER_MODE) ? "Normal" : "Bootloader");
    printf("  PHI2:   %lu Hz\n", (unsigned long)mia_applied_phi2_hz);
    printf("  RAM:    %uKB  ($00000-$%05X)\n", MIA_RAM_SIZE / 1024, MIA_RAM_SIZE - 1);
    printf("  Status: 0x%04X", st);

    bool any = false;
    if (st & MIA_STAT_MASTER_MODE)           { printf(any ? "," : " ("); printf("NORMAL");   any = true; }
    if (st & MIA_STAT_ERRORS)                { printf(any ? "," : " ("); printf("ERRORS");   any = true; }
    if (st & MIA_STAT_CMD_RUNNING)           { printf(any ? "," : " ("); printf("CMD");      any = true; }
    if (st & MIA_STAT_DMA_RUNNING)           { printf(any ? "," : " ("); printf("DMA");      any = true; }
    if (st & MIA_STAT_SPEED_CHANGING)        { printf(any ? "," : " ("); printf("SPEED");    any = true; }
    if (st & MIA_STAT_VIDEO_FRAME_REQUESTED) { printf(any ? "," : " ("); printf("VID_REQ");  any = true; }
    if (st & MIA_STAT_VIDEO_FRAME_SENT)      { printf(any ? "," : " ("); printf("VID_SENT"); any = true; }
    if (any) printf(")");
    printf("\n");

    printf("  IDXA:   index %u\n", mia_regs->idxa_selector);
    printf("  IDXB:   index %u\n", mia_regs->idxb_selector);

    mia_net_wifi_print_status();
    mia_input_print_status();
}

static void cmd_errors_list(void) {
    uint8_t first = _err_first;
    uint8_t last = _err_last;
    uint8_t count = (uint8_t)((last - first) & 15u);

    printf("MIA Errors: %u queued", (unsigned)count);
    if (count != 0) {
        printf("  current: 0x%02X %s", (unsigned)mia_regs->mia_error, error_name((uint8_t)mia_regs->mia_error));
    }
    printf("\n");

    if (count == 0) {
        printf("  none\n");
        return;
    }

    for (uint8_t i = 0; i < count && i < 15u; i++) {
        uint8_t pos = (first + i) & 15u;
        uint8_t code = _err_buf[pos];
        printf("  %2u: 0x%02X %s\n", (unsigned)i, (unsigned)code, error_name(code));
    }
}

static void cmd_errors(const char *args) {
    args = skip_ws(args);

    if (strcmp(args, "list") == 0) {
        cmd_errors_list();
        return;
    }

    if (strcmp(args, "clear") == 0) {
        error_reset();
        printf("MIA Errors cleared.\n");
        return;
    }

    printf("Usage: errors [list|clear]\n");
}

static void cmd_speed(const char *args) {
    args = skip_ws(args);

    if (!*args) {
        printf("PHI2: %lu Hz\n", (unsigned long)mia_applied_phi2_hz);
        printf("Usage: speed HZ  (range: %u-%u, e.g. speed 1000000)\n",
               MIA_MIN_PHI2_HZ, MIA_MAX_PHI2_HZ);
        return;
    }

    uint32_t hz = 0;
    bool found = false;
    while (*args >= '0' && *args <= '9') {
        uint32_t d = (uint32_t)(*args - '0');
        if (hz > (UINT32_MAX - d) / 10) { hz = MIA_MAX_PHI2_HZ + 1u; break; }
        hz = hz * 10 + d;
        args++;
        found = true;
    }

    if (!found) {
        printf("Invalid value. Usage: speed HZ  (e.g. speed 1000000)\n");
        return;
    }

    mia_staged_phi2_hz = hz;
    mia_speed_commit();
    printf("PHI2 speed requested: %lu Hz (use 'status' to confirm applied value)\n",
           (unsigned long)hz);
}

static void cmd_wifi(const char *args) {
    args = skip_ws(args);

    if (!*args || strcmp(args, "status") == 0) {
        mia_net_wifi_print_status();
        return;
    }

    if (strcmp(args, "off") == 0) {
        mia_net_wifi_off();
        return;
    }

    bool is_ap      = (strncmp(args, "ap",      2) == 0 && (args[2] == ' ' || args[2] == '\t' || args[2] == '\0'));
    bool is_connect = (strncmp(args, "connect",  7) == 0 && (args[7] == ' ' || args[7] == '\t' || args[7] == '\0'));

    if (!is_ap && !is_connect) {
        printf("Usage: wifi [status|off|connect <ssid> [password]|ap <ssid> [password]]\n");
        printf("  Notes: SSID and password may not contain spaces.\n");
        printf("         AP clients must use a static IP; no DHCP is provided.\n");
        return;
    }

    args = skip_ws(args + (is_ap ? 2 : 7));

    char ssid[64] = {0};
    int i = 0;
    while (*args && *args != ' ' && *args != '\t' && i < 63) ssid[i++] = *args++;
    if (!ssid[0]) {
        printf("Usage: wifi %s <ssid> [password]\n", is_ap ? "ap" : "connect");
        return;
    }

    args = skip_ws(args);

    if (is_ap) {
        mia_net_wifi_start_ap(ssid, args);
    } else {
        mia_net_wifi_connect(ssid, args);
    }
}

static void cmd_monitor(const char *args) {
    (void)args;
    con_enter_monitor();
}

static void cmd_input(const char *args) {
    args = skip_ws(args);

    if (!*args || strcmp(args, "status") == 0) {
        mia_input_print_status();
        printf("Usage: input [status|console|wifi]\n");
        return;
    }

    if (strcmp(args, "console") == 0) {
        if (!mia_input_set_mode(MIA_INPUT_MODE_CONSOLE)) {
            printf("Input: console mode is not available in this build.\n");
            return;
        }
        con_enter_input();
        return;
    }

    if (strcmp(args, "wifi") == 0) {
        if (!mia_input_set_mode(MIA_INPUT_MODE_WIFI)) {
            printf("Input: Wi-Fi mode is not available.\n");
            return;
        }
        printf("Input: Wi-Fi mode active on UDP port %u.\n", (unsigned)MIA_INPUT_UDP_PORT);
        return;
    }

    printf("Usage: input [status|console|wifi]\n");
}

static void cmd_quit(const char *args) {
    (void)args;
    printf("Rebooting to BOOTSEL...\n");
    reset_usb_boot(0, 0);
}

// ---- Command table ----------------------------------------------------------

typedef void (*con_cmd_fn_t)(const char *args);

typedef struct {
    const char    *name;
    con_cmd_fn_t   fn;
    const char    *help;
} con_cmd_t;

static const con_cmd_t commands[] = {
    { "status",  cmd_status,  "Show MIA status and Wi-Fi state"          },
    { "errors",  cmd_errors,  "errors [list|clear]"                      },
    { "speed",   cmd_speed,   "speed HZ  — set PHI2 clock frequency"     },
    { "wifi",    cmd_wifi,    "wifi [status|off|connect|ap]"             },
    { "input",   cmd_input,   "input [status|console|wifi]"              },
    { "monitor", cmd_monitor, "Enter 65C02 machine language monitor"     },
    { "quit",    cmd_quit,    "Reboot to BOOTSEL"                        },
    { NULL, NULL, NULL }
};

static void cmd_help(void) {
    printf("Commands:\n");
    for (const con_cmd_t *c = commands; c->name; c++) {
        printf("  %-10s %s\n", c->name, c->help);
    }
    printf("  help       Show this help\n");
}

// ---- Dispatcher (called by con.c) ------------------------------------------

void con_dispatch(const char *line) {
    line = skip_ws(line);
    if (!*line) return;

    char cmd[16] = {0};
    int ci = 0;
    const char *p = line;
    while (*p && *p != ' ' && *p != '\t' && ci < 15) cmd[ci++] = *p++;

    if (strcmp(cmd, "?") == 0 || strcmp(cmd, "help") == 0) {
        cmd_help();
        return;
    }

    for (const con_cmd_t *c = commands; c->name; c++) {
        if (strcmp(cmd, c->name) == 0) {
            c->fn(p);
            return;
        }
    }

    printf("Unknown command '%s'. Try 'help'.\n", cmd);
}
