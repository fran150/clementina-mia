#include "debug_helper.h"
#include <stdarg.h>
#include "pico/stdio.h"
#include <string.h>

#define PARAM_SIZE      10
#define BUFFER_SIZE     20
#define COMMAND_COUNT   1

volatile log_level_t current_log_level = LOG_INFO;

typedef int (*IntFuncPtr)(char*); 

static bool debug_enabled = false;
static int buffer_index = 0;
static char command_buffer[BUFFER_SIZE] = { '\0' };

const char *command_strings[COMMAND_COUNT] = {
    "log lvl %9s",
};

int log_level_toggle(char *level) {
    if (strcmp(level, "error") == 0) {
        current_log_level = LOG_ERROR;
    } else if (strcmp(level, "warn") == 0) {
        current_log_level = LOG_WARN;
    } else if (strcmp(level, "info") == 0) {
        current_log_level = LOG_INFO;
    } else if (strcmp(level, "debug") == 0) {
        current_log_level = LOG_DEBUG;
    } else if (strcmp(level, "trace") == 0) {
        current_log_level = LOG_TRACE;
    } else {
        printf("Invalid log level: %s\n", level);
        return -1;
    }

    printf("Log level set to %s\n", level);
    return 0;
}

const IntFuncPtr command_function[] = {
    log_level_toggle
};

void debug_print(const char *format, ...) {
    if (debug_enabled) {
        printf(format);
    }
}

void process_command_buffer() {
    if (command_buffer[0] == '\0') return;

    for (int i = 0; i < COMMAND_COUNT; i++) {
        char parameter[PARAM_SIZE];
        
        if (sscanf(command_buffer, command_strings[i], parameter) == 1) {
            if (command_function[i](parameter) < 0) {
                printf("Invalid command: %s\n", command_buffer);
            }

            return;
        }
    }

    printf("Unknown command: %s\n", command_buffer);
}

void eval_keyboard() {
    int ch = stdio_getchar_timeout_us(0);  // Non-blocking check

    if (ch == PICO_ERROR_TIMEOUT) {
        return;
    }

    if (buffer_index >= BUFFER_SIZE - 1) {
        printf("Command too long\n");
        buffer_index = 0;
        return;
    }

    if (ch == '\n' || ch == '\r') {
        process_command_buffer();
        buffer_index = 0;
    } else {
        command_buffer[buffer_index++] = ch;
    }

    command_buffer[buffer_index] = '\0';
}
