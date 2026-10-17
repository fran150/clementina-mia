#include "con_keys.h"

#define CHR_ESC 0x1Bu

enum { KEYS_IDLE, KEYS_ESC, KEYS_CSI, KEYS_SS3 };

// The key code for a sequence's final byte, or 0 to drop it. The codes match
// input_state.c's HID decode: PETSCII cursor and Home, $94 Insert, $7F Delete.
static uint8_t keys_final(uint8_t final, uint8_t param) {
    switch (final) {
    case 'A': return 0x91u;   // Up
    case 'B': return 0x11u;   // Down
    case 'C': return 0x1Du;   // Right
    case 'D': return 0x9Du;   // Left
    case 'H': return 0x13u;   // Home
    case '~':
        switch (param) {
        case 1:
        case 7: return 0x13u; // Home
        case 2: return 0x94u; // Insert
        case 3: return 0x7Fu; // Delete
        default: return 0u;   // End, Page Up, Page Down, function keys
        }
    default:
        return 0u;
    }
}

// A byte outside any sequence: Enter and Backspace as the BASIC editor wants
// them, an ESC starts a sequence, everything else passes through.
static int keys_idle(con_keys_t *keys, uint8_t c, uint64_t now_us, uint8_t *out) {
    if (c == CHR_ESC) {
        keys->state = KEYS_ESC;
        keys->esc_at_us = now_us;
        return 0;
    }
    if (c == '\n') {
        c = '\r';
    } else if (c == 0x7Fu) {
        c = '\b';             // terminals send Backspace as DEL
    }
    out[0] = c;
    return 1;
}

void con_keys_reset(con_keys_t *keys) {
    keys->state = KEYS_IDLE;
    keys->param = 0;
    keys->param_done = false;
    keys->esc_at_us = 0;
}

int con_keys_feed(con_keys_t *keys, uint8_t c, uint64_t now_us, uint8_t out[2]) {
    uint8_t code;

    switch (keys->state) {
    case KEYS_ESC:
        if (c == '[') {
            keys->state = KEYS_CSI;
            keys->param = 0;
            keys->param_done = false;
            return 0;
        }
        if (c == 'O') {
            keys->state = KEYS_SS3;
            return 0;
        }
        // Esc followed by an ordinary key (or Alt+key): both go out.
        keys->state = KEYS_IDLE;
        out[0] = CHR_ESC;
        return 1 + keys_idle(keys, c, now_us, out + 1);

    case KEYS_CSI:
        if (c >= '0' && c <= '9') {
            if (!keys->param_done && keys->param < 99u) {
                keys->param = (uint8_t)(keys->param * 10u + (c - '0'));
            }
            return 0;
        }
        if (c >= 0x20u && c <= 0x3Fu) {
            keys->param_done = true;  // ';' and other parameter bytes
            return 0;
        }
        keys->state = KEYS_IDLE;
        if (c >= 0x40u && c <= 0x7Eu) {
            code = keys_final(c, keys->param);
            if (code == 0u) return 0;
            out[0] = code;
            return 1;
        }
        // A control byte cut the sequence short: drop it, keep the byte.
        return keys_idle(keys, c, now_us, out);

    case KEYS_SS3:
        keys->state = KEYS_IDLE;
        code = keys_final(c, 0);
        if (code == 0u) return 0;
        out[0] = code;
        return 1;

    default:
        return keys_idle(keys, c, now_us, out);
    }
}

int con_keys_expire(con_keys_t *keys, uint64_t now_us, uint8_t out[2]) {
    if (keys->state == KEYS_IDLE || now_us - keys->esc_at_us < CON_KEYS_ESC_TIMEOUT_US) {
        return 0;
    }
    if (keys->state == KEYS_ESC) {
        keys->state = KEYS_IDLE;
        out[0] = CHR_ESC;
        return 1;
    }
    keys->state = KEYS_IDLE;
    return 0;
}
