#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>

#include "pico/bootrom.h"

#include "etc/err.h"
#include "etc/status.h"
#include "audio/audio.h"
#include "irq/irq.h"
#include "mem/indexes.h"
#include "mem/regs.h"
#include "mem/mem.h"
#include "input/input.h"
#include "sys/exec.h"
#include "sys/speed.h"
#include "net/wifi.h"
#include "video/video.h"

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
        case ERROR_AUDIO_QUEUE_OVERFLOW: return "ERROR_AUDIO_QUEUE_OVERFLOW";
        default: return "UNKNOWN_ERROR";
    }
}

static void print_flag_name(bool *any, const char *name) {
    printf(*any ? "," : " (");
    printf("%s", name);
    *any = true;
}

static uint8_t error_queue_count(void) {
    return (uint8_t)((_err_last - _err_first) & 15u);
}

static void print_status_word(uint16_t st) {
    bool any = false;

    printf("0x%04X", st);
    if (st & MIA_STAT_MASTER_MODE)           print_flag_name(&any, "NORMAL");
    if (st & MIA_STAT_ERRORS)                print_flag_name(&any, "ERRORS");
    if (st & MIA_STAT_CMD_RUNNING)           print_flag_name(&any, "CMD");
    if (st & MIA_STAT_DMA_RUNNING)           print_flag_name(&any, "DMA");
    if (st & MIA_STAT_SPEED_CHANGING)        print_flag_name(&any, "SPEED");
    if (st & MIA_STAT_VIDEO_FRAME_REQUESTED) print_flag_name(&any, "VID_REQ");
    if (st & MIA_STAT_VIDEO_FRAME_SENT)      print_flag_name(&any, "VID_SENT");
    if (st & MIA_STAT_EXEC_PAUSED)           print_flag_name(&any, "PAUSED");
    if (st & MIA_STAT_AUDIO_ACTIVE)          print_flag_name(&any, "AUDIO");
    if (any) printf(")");
}

static void print_irq_sources(uint16_t flags) {
    bool any = false;

    if (flags & IRQ_ERROR)               print_flag_name(&any, "ERROR");
    if (flags & IRQ_IDXA_WRAPPED)        print_flag_name(&any, "IDXA_WRAP");
    if (flags & IRQ_IDXB_WRAPPED)        print_flag_name(&any, "IDXB_WRAP");
    if (flags & IRQ_COMMAND)             print_flag_name(&any, "COMMAND");
    if (flags & IRQ_SPEED_CHANGED)       print_flag_name(&any, "SPEED");
    if (flags & IRQ_VIDEO_FRAME_REQUEST) print_flag_name(&any, "VID_REQ");
    if (flags & IRQ_VIDEO_FRAME_SENT)    print_flag_name(&any, "VID_SENT");
    if (flags & IRQ_VIDEO_FRAME_ACKED)   print_flag_name(&any, "VID_ACK");
    if (flags & IRQ_INPUT_KEYBOARD)      print_flag_name(&any, "INPUT_KEY");
    if (flags & IRQ_INPUT_MOUSE)         print_flag_name(&any, "INPUT_MOUSE");
    if (flags & IRQ_INPUT_GAMEPAD)       print_flag_name(&any, "INPUT_PAD");
    if (flags & IRQ_TRIGGERED)           print_flag_name(&any, "TRIGGERED");

    if (!any) {
        printf(" none");
        return;
    }
    printf(")");
}

static void print_index_detail(uint8_t index_id) {
    volatile index_t *entry = &idx[index_id];
    uint8_t flags = entry->flags;

    printf("  index %u: current:$%06lX  default:$%06lX  limit:$%06lX  step:%u  flags:0x%02X",
           (unsigned)index_id,
           (unsigned long)(entry->current_addr & MASK_24BIT),
           (unsigned long)(entry->default_addr & MASK_24BIT),
           (unsigned long)(entry->limit_addr & MASK_24BIT),
           (unsigned)entry->step,
           (unsigned)flags);

    bool any = false;
    if (flags & (1u << IDX_FLAG_R_STP_ENA)) print_flag_name(&any, "R_STEP");
    if (flags & (1u << IDX_FLAG_W_STP_ENA)) print_flag_name(&any, "W_STEP");
    if (flags & (1u << IDX_FLAG_STP_DIR))   print_flag_name(&any, "BACKWARD");
    if (flags & (1u << IDX_FLAG_WRAP_ENA))  print_flag_name(&any, "WRAP");
    if (flags & (1u << IDX_FLAG_WRAP_IRQ))  print_flag_name(&any, "WRAP_IRQ");
    if (any) printf(")");
    printf("\n");
}

static bool parse_u8_arg(const char *args, uint8_t *value) {
    args = skip_ws(args);
    if (!*args) {
        return false;
    }

    uint32_t base = 10;
    if (*args == '$') {
        base = 16;
        args++;
    } else if (args[0] == '0' && (args[1] == 'x' || args[1] == 'X')) {
        base = 16;
        args += 2;
    }

    uint32_t parsed = 0;
    bool found = false;
    while (*args) {
        uint8_t digit;
        if (*args >= '0' && *args <= '9') {
            digit = (uint8_t)(*args - '0');
        } else if (base == 16 && *args >= 'a' && *args <= 'f') {
            digit = (uint8_t)(*args - 'a' + 10);
        } else if (base == 16 && *args >= 'A' && *args <= 'F') {
            digit = (uint8_t)(*args - 'A' + 10);
        } else {
            break;
        }

        if (digit >= base || parsed > (255u - digit) / base) {
            return false;
        }

        parsed = parsed * base + digit;
        args++;
        found = true;
    }

    args = skip_ws(args);
    if (*args || !found) {
        return false;
    }

    *value = (uint8_t)parsed;
    return true;
}

static void cmd_errors_list(void);

// ---- Command handlers -------------------------------------------------------

static void cmd_status_summary(void) {
    uint16_t st = mia_regs->mia_status;
    uint8_t err_count = error_queue_count();

    printf("MIA Status:\n");
    printf("  Mode:   %s\n", (st & MIA_STAT_MASTER_MODE) ? "Normal" : "Bootloader");
    printf("  Exec:   %s\n", mia_exec_is_paused() ? "Paused (PHI2 stopped low)" : "Running");
    printf("  PHI2:   %lu Hz", (unsigned long)mia_applied_phi2_hz);
    if (mia_speed_change_requested || (st & MIA_STAT_SPEED_CHANGING)) {
        printf("  requested:%lu Hz", (unsigned long)mia_requested_phi2_hz);
    }
    printf("\n");
    printf("  RAM:    %uKB  ($00000-$%05X)\n", MIA_RAM_SIZE / 1024, MIA_RAM_SIZE - 1);
    printf("  Status: ");
    print_status_word(st);
    printf("\n");
    printf("  IRQ:    status:0x%04X  mask:0x%04X\n",
           mia_regs->irq_status, mia_regs->irq_mask);
    printf("  Errors: %u queued", (unsigned)err_count);
    if (err_count != 0) {
        printf("  current:0x%02X %s",
               (unsigned)mia_regs->mia_error,
               error_name((uint8_t)mia_regs->mia_error));
    }
    printf("\n");

    printf("  IDXA:   index %u\n", mia_regs->idxa_selector);
    printf("  IDXB:   index %u\n", mia_regs->idxb_selector);

    mia_net_wifi_print_status();
    mia_video_print_summary();
    mia_input_print_status();
    mia_audio_print_summary();
}

static void cmd_status_irq(void) {
    uint16_t status = mia_regs->irq_status;
    uint16_t mask = mia_regs->irq_mask;
    uint16_t requests = atomic_load(&mia_irq_set_requests);

    printf("IRQ:\n");
    printf("  status:   0x%04X", status);
    print_irq_sources(status);
    printf("\n");
    printf("  mask:     0x%04X", mask);
    print_irq_sources(mask);
    printf("\n");
    printf("  enabled:  0x%04X", (uint16_t)(status & mask));
    print_irq_sources((uint16_t)(status & mask));
    printf("\n");
    printf("  requests: 0x%04X", requests);
    print_irq_sources(requests);
    printf("\n");
    printf("  line:     %s\n", (status & IRQ_TRIGGERED) ? "asserted" : "released");
}

static void cmd_status_speed(void) {
    printf("Speed:\n");
    printf("  applied:   %lu Hz\n", (unsigned long)mia_applied_phi2_hz);
    printf("  requested: %lu Hz\n", (unsigned long)mia_requested_phi2_hz);
    printf("  staged:    %lu Hz\n", (unsigned long)mia_staged_phi2_hz);
    printf("  pending:   %s\n", mia_speed_change_requested ? "yes" : "no");
    printf("  range:     %u-%u Hz\n", MIA_MIN_PHI2_HZ, MIA_MAX_PHI2_HZ);
}

static void cmd_status_index(const char *args) {
    uint8_t index_id;

    args = skip_ws(args);
    if (*args) {
        if (!parse_u8_arg(args, &index_id)) {
            printf("Usage: status index [id]\n");
            return;
        }
        printf("Index:\n");
        print_index_detail(index_id);
        return;
    }

    printf("Index:\n");
    printf("  IDXA selector: %u\n", mia_regs->idxa_selector);
    print_index_detail(mia_regs->idxa_selector);
    printf("  IDXB selector: %u\n", mia_regs->idxb_selector);
    print_index_detail(mia_regs->idxb_selector);
}

static void cmd_status_mem(void) {
    printf("Memory:\n");
    printf("  RAM: %uKB  range:$00000-$%05X  mask:0x%05X\n",
           MIA_RAM_SIZE / 1024,
           MIA_RAM_SIZE - 1,
           MIA_RAM_MASK);
    printf("  regs: %u bytes at %p\n", (unsigned)sizeof(*mia_regs), (const void *)(uintptr_t)mia_regs);
    printf("  selected indexes:\n");
    print_index_detail(mia_regs->idxa_selector);
    print_index_detail(mia_regs->idxb_selector);
}

static void cmd_status(const char *args) {
    args = skip_ws(args);

    if (!*args || strcmp(args, "summary") == 0 || strcmp(args, "all") == 0) {
        cmd_status_summary();
        return;
    }

    if (strcmp(args, "video") == 0) {
        mia_video_print_status();
        return;
    }

    if (strcmp(args, "input") == 0) {
        mia_input_print_detail();
        return;
    }

    if (strcmp(args, "audio") == 0) {
        mia_audio_print_status();
        return;
    }

    if (strcmp(args, "wifi") == 0) {
        mia_net_wifi_print_detail();
        return;
    }

    if (strcmp(args, "irq") == 0) {
        cmd_status_irq();
        return;
    }

    if (strcmp(args, "speed") == 0) {
        cmd_status_speed();
        return;
    }

    if (strcmp(args, "exec") == 0) {
        mia_exec_print_status();
        return;
    }

    if (strcmp(args, "errors") == 0) {
        cmd_errors_list();
        return;
    }

    if (strncmp(args, "index", 5) == 0 && (args[5] == '\0' || args[5] == ' ' || args[5] == '\t')) {
        cmd_status_index(args + 5);
        return;
    }

    if (strcmp(args, "mem") == 0 || strcmp(args, "memory") == 0) {
        cmd_status_mem();
        return;
    }

    printf("Usage: status [video|input|audio|wifi|irq|speed|exec|errors|mem|index [id]]\n");
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

static void cmd_audio(const char *args) {
    args = skip_ws(args);

    if (!*args || strcmp(args, "status") == 0) {
        mia_audio_print_status();
        printf("Usage: audio [status|enable|stop|reset]\n");
        return;
    }

    if (strcmp(args, "enable") == 0) {
        mia_audio_enable();
        printf("Audio: enabled\n");
        return;
    }

    if (strcmp(args, "stop") == 0) {
        mia_audio_stop();
        printf("Audio: stopped\n");
        return;
    }

    if (strcmp(args, "reset") == 0) {
        mia_audio_reset();
        printf("Audio: reset\n");
        return;
    }

    printf("Usage: audio [status|enable|stop|reset]\n");
}

static void cmd_exec(const char *args) {
    args = skip_ws(args);

    if (!*args || strcmp(args, "status") == 0) {
        mia_exec_print_status();
        printf("Usage: exec [status|pause|resume]\n");
        return;
    }

    if (strcmp(args, "pause") == 0) {
        mia_exec_pause();
        printf("Exec: paused\n");
        return;
    }

    if (strcmp(args, "resume") == 0) {
        mia_exec_resume();
        printf("Exec: running\n");
        return;
    }

    printf("Usage: exec [status|pause|resume]\n");
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
    { "status",  cmd_status,  "status [video|input|audio|wifi|irq|speed|exec|mem|index]" },
    { "errors",  cmd_errors,  "errors [list|clear]"                      },
    { "speed",   cmd_speed,   "speed HZ  — set PHI2 clock frequency"     },
    { "wifi",    cmd_wifi,    "wifi [status|off|connect|ap]"             },
    { "input",   cmd_input,   "input [status|console|wifi]"              },
    { "audio",   cmd_audio,   "audio [status|enable|stop|reset]"         },
    { "exec",    cmd_exec,    "exec [status|pause|resume]"               },
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
