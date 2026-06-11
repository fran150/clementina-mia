#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "pico/bootrom.h"
#include "pico/stdlib.h"

#include "con.h"
#include "monitor.h"
#include "mem/regs.h"
#include "mem/mem.h"
#include "etc/status.h"
#include "sys/speed.h"

#define CON_LINE_MAX 80

static char   line_buf[CON_LINE_MAX];
static int    line_len      = 0;
static bool   prompt_shown  = false;

// ---- Status display ------------------------------------------------------

static void print_status(void) {
    uint16_t st = mia_regs->mia_status;

    printf("MIA Status:\n");
    printf("  Mode:   %s\n", (st & MIA_STAT_MASTER_MODE) ? "Normal" : "Bootloader");
    printf("  PHI2:   %lu Hz\n", (unsigned long)mia_applied_phi2_hz);
    printf("  RAM:    %uKB  ($00000-$%05X)\n", MIA_RAM_SIZE / 1024, MIA_RAM_SIZE - 1);
    printf("  Status: 0x%04X", st);

    bool any = false;
    if (st & MIA_STAT_MASTER_MODE)           { printf(any?",":" ("); printf("NORMAL");      any=true; }
    if (st & MIA_STAT_ERRORS)                { printf(any?",":" ("); printf("ERRORS");      any=true; }
    if (st & MIA_STAT_CMD_RUNNING)           { printf(any?",":" ("); printf("CMD");         any=true; }
    if (st & MIA_STAT_DMA_RUNNING)           { printf(any?",":" ("); printf("DMA");         any=true; }
    if (st & MIA_STAT_SPEED_CHANGING)        { printf(any?",":" ("); printf("SPEED");       any=true; }
    if (st & MIA_STAT_VIDEO_FRAME_REQUESTED) { printf(any?",":" ("); printf("VID_REQ");     any=true; }
    if (st & MIA_STAT_VIDEO_FRAME_SENT)      { printf(any?",":" ("); printf("VID_SENT");    any=true; }
    if (any) printf(")");
    printf("\n");

    printf("  IDXA:   index %u\n", mia_regs->idxa_selector);
    printf("  IDXB:   index %u\n", mia_regs->idxb_selector);
}

// ---- Normal-mode command dispatch ----------------------------------------

static void con_dispatch(const char *line) {
    while (*line == ' ' || *line == '\t') line++;
    if (!*line) return;

    // Extract first word only so trailing spaces don't break matching
    char cmd[16] = {0};
    int ci = 0;
    const char *p = line;
    while (*p && *p != ' ' && *p != '\t' && ci < 15) cmd[ci++] = *p++;

    if (strcmp(cmd, "quit") == 0) {
        printf("Rebooting to BOOTSEL...\n");
        reset_usb_boot(0, 0);
    } else if (strcmp(cmd, "monitor") == 0) {
        monitor_run();
    } else if (strcmp(cmd, "status") == 0) {
        print_status();
    } else if (strcmp(cmd, "?") == 0 || strcmp(cmd, "help") == 0) {
        printf("Commands: monitor, status, quit\n");
    } else {
        printf("Unknown command '%s'. Try: monitor, status, quit\n", cmd);
    }
}

// ---- Public API ----------------------------------------------------------

void con_read_line(char *buf, int max_len) {
    int len = 0;
    while (true) {
        int c = getchar();
        if (c == '\r' || c == '\n') {
            buf[len] = '\0';
            printf("\n");
            return;
        } else if ((c == '\b' || c == 127) && len > 0) {
            len--;
            printf("\b \b");
            fflush(stdout);
        } else if (c >= 0x20 && c < 0x7F && len < max_len - 1) {
            buf[len++] = (char)c;
            printf("%c", (char)c);
            fflush(stdout);
        }
    }
}

void con_process(void) {
    if (!prompt_shown) {
        printf("> ");
        fflush(stdout);
        prompt_shown = true;
    }

    int c = getchar_timeout_us(0);
    if (c == PICO_ERROR_TIMEOUT) return;

    if (c == '\r' || c == '\n') {
        printf("\n");
        line_buf[line_len] = '\0';
        con_dispatch(line_buf);
        line_len     = 0;
        prompt_shown = false;
    } else if ((c == '\b' || c == 127) && line_len > 0) {
        line_len--;
        printf("\b \b");
        fflush(stdout);
    } else if (c >= 0x20 && c < 0x7F && line_len < CON_LINE_MAX - 1) {
        line_buf[line_len++] = (char)c;
        printf("%c", (char)c);
        fflush(stdout);
    }
}
