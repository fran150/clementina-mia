#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "pico/stdlib.h"

#include "con.h"
#include "monitor.h"

#define CON_LINE_MAX 80

typedef enum { CON_MODE_NORMAL, CON_MODE_MONITOR } con_mode_t;

static char       line_buf[CON_LINE_MAX];
static int        line_len     = 0;
static bool       prompt_shown = false;
static con_mode_t con_mode     = CON_MODE_NORMAL;

// Defined in con_cmds.c — dispatches a normal-mode command line.
extern void con_dispatch(const char *line);

// Called by con_cmds.c when the "monitor" command is issued.
void con_enter_monitor(void) {
    con_mode = CON_MODE_MONITOR;
    monitor_print_banner();
}

void con_process(void) {
    if (!prompt_shown) {
        printf(con_mode == CON_MODE_MONITOR ? "MON> " : "> ");
        fflush(stdout);
        prompt_shown = true;
    }

    int c = getchar_timeout_us(0);
    if (c == PICO_ERROR_TIMEOUT) return;

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
