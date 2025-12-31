#ifndef DEBUG_HELPER_H
#define DEBUG_HELPER_H

#include <stdio.h>
#include <stdbool.h>

typedef enum {
    LOG_ERROR = 0,
    LOG_WARN,
    LOG_INFO,
    LOG_DEBUG,
    LOG_TRACE
} log_level_t;

extern volatile log_level_t current_log_level;

#define log_print(level, fmt, ...)                           \
    do {                                                     \
        if ((level) <= current_log_level) {                  \
            printf(fmt "\n", ##__VA_ARGS__);                 \
        }                                                    \
    } while (0)

void eval_keyboard();


#endif