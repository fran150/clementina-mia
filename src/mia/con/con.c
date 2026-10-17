#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#include "pico/stdlib.h"

#include "con.h"
#include "con_keys.h"
#include "input/input.h"
#include "monitor.h"

#define CON_LINE_MAX 80
#define CTRL_Q 0x11

typedef enum { CON_MODE_NORMAL, CON_MODE_MONITOR, CON_MODE_INPUT } con_mode_t;

static char       line_buf[CON_LINE_MAX];
static int        line_len     = 0;
static bool       prompt_shown = false;
static con_mode_t con_mode     = CON_MODE_NORMAL;
static con_keys_t con_keys;

// Defined in con_cmds.c — dispatches a normal-mode command line.
extern void con_dispatch(const char *line);

// Called by con_cmds.c when the "monitor" command is issued.
void con_enter_monitor(void) {
    con_mode = CON_MODE_MONITOR;
    monitor_print_banner();
}

void con_enter_input(void) {
    con_mode = CON_MODE_INPUT;
    prompt_shown = true;
    con_keys_reset(&con_keys);
    printf("Console input active. Press Ctrl+Q to return to commands.\n");
}

static void con_send_keys(const uint8_t *keys, int count) {
    for (int i = 0; i < count; i++) {
        mia_input_console_byte(keys[i]);
    }
}

// Terminal bytes go to the text FIFO through the key decoder, so the cursor,
// Home, Insert and Delete keys arrive as Clementina key codes. Ctrl+Q ends.
static bool con_process_input_byte(int c) {
    uint8_t keys[2];

    if (c == CTRL_Q) {
        con_keys_reset(&con_keys);
        mia_input_console_end_capture();
        con_mode = CON_MODE_NORMAL;
        line_len = 0;
        prompt_shown = false;
        printf("\nConsole input ended.\n");
        return true;
    }

    con_send_keys(keys, con_keys_feed(&con_keys, (uint8_t)c, time_us_64(), keys));
    return true;
}

void con_process(void) {
    if (!prompt_shown) {
        printf(con_mode == CON_MODE_MONITOR ? "MON> " : "> ");
        fflush(stdout);
        prompt_shown = true;
    }

    int c = getchar_timeout_us(0);
    if (c == PICO_ERROR_TIMEOUT) {
        if (con_mode == CON_MODE_INPUT) {
            uint8_t keys[2];
            con_send_keys(keys, con_keys_expire(&con_keys, time_us_64(), keys));
        }
        return;
    }

    if (con_mode == CON_MODE_INPUT) {
        (void)con_process_input_byte(c);
        return;
    }

    if (c == '\r' || c == '\n') {
        printf("\n");
        line_buf[line_len] = '\0';

        if (con_mode == CON_MODE_MONITOR) {
            if (!monitor_exec_line(line_buf)) {
                con_mode = CON_MODE_NORMAL;
                printf("Exiting monitor.\n");
            }
        } else {
            con_dispatch(line_buf);
        }

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
