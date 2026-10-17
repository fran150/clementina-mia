#ifndef _MIA_CON_CON_KEYS_H_
#define _MIA_CON_CON_KEYS_H_

#include <stdbool.h>
#include <stdint.h>

// Console input mode's key decoder. A terminal sends its cursor, Home, Insert
// and Delete keys as escape sequences (ESC [ A, ESC O A, ESC [ 3 ~, ...); this
// turns them into the key codes MIA's keyboard decode gives those keys, so
// BASIC's screen editor sees the same bytes from either input source. Other
// sequences are dropped. A lone Esc can't be told from the start of a sequence
// until CON_KEYS_ESC_TIMEOUT_US passes without another byte.

#define CON_KEYS_ESC_TIMEOUT_US 20000u

typedef struct {
    uint8_t  state;
    uint8_t  param;       // first number of an ESC [ sequence, capped at 99
    bool     param_done;  // a ';' ended the first number
    uint64_t esc_at_us;   // when the sequence's ESC arrived
} con_keys_t;

// Forget any partial sequence.
void con_keys_reset(con_keys_t *keys);

// Decode one terminal byte received at now_us. Writes the text FIFO bytes it
// produces (0 to 2) to out and returns how many.
int con_keys_feed(con_keys_t *keys, uint8_t c, uint64_t now_us, uint8_t out[2]);

// Call when no byte arrived. Once the timeout passes, a lone ESC goes out as
// $1B (written to out, returns 1) and an unfinished sequence is dropped.
int con_keys_expire(con_keys_t *keys, uint64_t now_us, uint8_t out[2]);

#endif
