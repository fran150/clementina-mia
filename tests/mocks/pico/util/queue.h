/**
 * Mock implementation of pico/util/queue.h for testing
 */

#ifndef PICO_UTIL_QUEUE_H
#define PICO_UTIL_QUEUE_H

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    void* data;
    uint16_t element_size;
    uint16_t element_count;
    uint16_t wptr;
    uint16_t rptr;
} queue_t;

static inline void queue_init(queue_t *q, unsigned int element_size, unsigned int element_count) {
    q->element_size = element_size;
    q->element_count = element_count;
    q->wptr = 0;
    q->rptr = 0;
    q->data = malloc(element_size * element_count);
}

static inline bool queue_try_add(queue_t *q, const void *data) {
    uint16_t next_wptr = (q->wptr + 1) % q->element_count;
    if (next_wptr == q->rptr) return false; // Full
    
    memcpy((char*)q->data + (q->wptr * q->element_size), data, q->element_size);
    q->wptr = next_wptr;
    return true;
}

static inline bool queue_try_remove(queue_t *q, void *data) {
    if (q->rptr == q->wptr) return false; // Empty
    
    memcpy(data, (char*)q->data + (q->rptr * q->element_size), q->element_size);
    q->rptr = (q->rptr + 1) % q->element_count;
    return true;
}

#endif // PICO_UTIL_QUEUE_H
