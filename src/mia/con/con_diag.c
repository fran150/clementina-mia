// Diagnostic console commands: status ctx, status pio, dump and trace.
// See docs/diagnostics.md for what each prints and the snapshot file format.
//
// Everything here runs on core 0 from the console. It reads state that core 1
// and the PIOs own without stopping them, so it never touches bus timing; a
// copy taken while the 6502 runs can be torn, which is why the file dumps
// suggest `exec pause` first. The bus event trace itself lives in core 1's
// action loop and exists only in the diagnostic build (MIA_DIAG, make diag).

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#include "hardware/pio.h"
#include "pico/time.h"

#include "con_diag.h"
#include "monitor.h"
#include "etc/err.h"
#include "etc/status.h"
#include "mem/indexes.h"
#include "mem/mem.h"
#include "mem/regs.h"
#include "sd/sd.h"
#include "sys/exec.h"
#include "sys/mia.h"
#include "sys/speed.h"

// ---- Helpers ---------------------------------------------------------------

static const char *skip_ws(const char *p) {
    while (*p == ' ' || *p == '\t') p++;
    return p;
}

// Copies the next word of *p into out (at most size - 1 chars) and advances
// *p past it. Returns false when there is no word.
static bool next_word(const char **p, char *out, size_t size) {
    const char *s = skip_ws(*p);
    size_t n = 0;
    while (*s && *s != ' ' && *s != '\t') {
        if (n + 1 < size) out[n++] = *s;
        s++;
    }
    out[n] = '\0';
    *p = s;
    return n != 0;
}

// Parses a hex number, with an optional $ or 0x prefix.
static bool parse_hex(const char *s, uint32_t *value) {
    if (*s == '$') {
        s++;
    } else if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2;
    }
    if (!*s) return false;

    uint32_t v = 0;
    for (; *s; s++) {
        uint32_t d;
        if (*s >= '0' && *s <= '9')      d = (uint32_t)(*s - '0');
        else if (*s >= 'a' && *s <= 'f') d = (uint32_t)(*s - 'a' + 10);
        else if (*s >= 'A' && *s <= 'F') d = (uint32_t)(*s - 'A' + 10);
        else return false;
        if (v > 0x0FFFFFFFu) return false;
        v = (v << 4) | d;
    }
    *value = v;
    return true;
}

// Parses an index id: decimal, or hex with $ or 0x.
static bool parse_index_id(const char *s, uint8_t *value) {
    uint32_t v = 0;
    if (*s == '$' || (s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))) {
        if (!parse_hex(s, &v)) return false;
    } else {
        if (!*s) return false;
        for (; *s; s++) {
            if (*s < '0' || *s > '9') return false;
            v = v * 10u + (uint32_t)(*s - '0');
            if (v > 255u) return false;
        }
    }
    if (v > 255u) return false;
    *value = (uint8_t)v;
    return true;
}

static const char *const reg_names[32] = {
    "IDXA_PORT",   "IDXA_SELECT", "CFG_SELECT",   "CFG_PORT",
    "IDXB_PORT",   "IDXB_SELECT", "CMD_PARAM1",   "CMD_PARAM2",
    "CMD_PARAM3",  "CMD_TRIGGER", "STATUS_L",     "STATUS_H",
    "ERROR_L",     "ERROR_H",     "IRQ_MASK_L",   "IRQ_MASK_H",
    "IRQ_STATUS_L","IRQ_STATUS_H","INPUT_STATUS", "INPUT_CHAR",
    "INPUT_COUNT", "MIA_CTX",     "(reserved)",   "(reserved)",
    "(reserved)",  "(reserved)",  "NMI_VEC_L",    "NMI_VEC_H",
    "RESET_VEC_L", "RESET_VEC_H", "IRQ_VEC_L",    "IRQ_VEC_H",
};

static bool index_in_use(const volatile index_t *entry) {
    for (unsigned i = 0; i < sizeof(entry->bytes); i++) {
        if (entry->bytes[i] != 0) return true;
    }
    return false;
}

static void print_index_line(uint8_t id, const index_t *entry) {
    printf("  %3u ($%02X): current:$%06lX default:$%06lX limit:$%06lX step:%u flags:$%02X\n",
           (unsigned)id, (unsigned)id,
           (unsigned long)(entry->current_addr & MASK_24BIT),
           (unsigned long)(entry->default_addr & MASK_24BIT),
           (unsigned long)(entry->limit_addr & MASK_24BIT),
           (unsigned)entry->step, (unsigned)entry->flags);
}

static index_t index_copy(uint8_t id) {
    index_t copy;
    for (unsigned i = 0; i < sizeof(copy.bytes); i++) {
        copy.bytes[i] = idx[id].bytes[i];
    }
    return copy;
}

static void print_running_note(void) {
    if (!mia_exec_is_paused()) {
        printf("  Note: the 6502 is running, so this copy may be torn. "
               "Use 'exec pause' first for an exact one.\n");
    }
}

// ---- status ctx --------------------------------------------------------------

void con_diag_status_ctx(void) {
    mia_ctx_t stack[MIA_CTX_DEPTH];
    uint8_t depth = mia_ctx_read(stack);

    printf("MIA_CTX:\n");
    printf("  depth: %u of %u\n", (unsigned)depth, (unsigned)MIA_CTX_DEPTH);
    for (uint8_t i = 0; i < depth; i++) {
        const mia_ctx_t *c = &stack[i];
        printf("  [%u] IDXA sel:%u  IDXB sel:%u  CFG sel:$%02X  params:$%02X $%02X $%02X\n",
               (unsigned)i, (unsigned)c->idxa_selector, (unsigned)c->idxb_selector,
               (unsigned)c->cfg_selector, (unsigned)c->cmd_param[0],
               (unsigned)c->cmd_param[1], (unsigned)c->cmd_param[2]);
        printf("      saved A:");
        print_index_line(c->idxa_selector, &c->idxa);
        printf("      saved B:");
        print_index_line(c->idxb_selector, &c->idxb);
    }
}

// ---- status pio --------------------------------------------------------------

static const char *const pio0_sm_names[4] = { "cs/rwb", "write+PHI2", "read", "unused" };
static const char *const pio1_sm_names[4] = { "action", "unused", "unused", "unused" };

// FDEBUG: RXSTALL bits 0-3, RXUNDER 8-11, TXOVER 16-19, TXSTALL 24-27, one
// bit per SM. FLEVEL: 4 bits TX then 4 bits RX per SM.
static void print_pio(const char *name, PIO pio, const char *const sm_names[4]) {
    uint32_t f = pio->fdebug;
    uint32_t level = pio->flevel;

    printf("  %s fdebug:$%08lX  RXSTALL:%lX RXUNDER:%lX TXOVER:%lX TXSTALL:%lX\n",
           name, (unsigned long)f,
           (unsigned long)(f & 0xFu), (unsigned long)((f >> 8) & 0xFu),
           (unsigned long)((f >> 16) & 0xFu), (unsigned long)((f >> 24) & 0xFu));
    for (unsigned sm = 0; sm < 4; sm++) {
        printf("    SM%u %-10s pc:%2lu  tx:%lu rx:%lu\n", sm, sm_names[sm],
               (unsigned long)(pio->sm[sm].addr & 0x1Fu),
               (unsigned long)((level >> (sm * 8u)) & 0xFu),
               (unsigned long)((level >> (sm * 8u + 4u)) & 0xFu));
    }
}

void con_diag_status_pio(const char *args) {
    args = skip_ws(args);

    if (strcmp(args, "clear") == 0) {
        pio0->fdebug = 0xFFFFFFFFu;  // write 1 to clear
        pio1->fdebug = 0xFFFFFFFFu;
        printf("PIO: fdebug flags cleared\n");
        return;
    }
    if (*args) {
        printf("Usage: status pio [clear]\n");
        return;
    }

    printf("Bus PIOs (fdebug flags are sticky until 'status pio clear'):\n");
    print_pio("pio0", pio0, pio0_sm_names);
    print_pio("pio1", pio1, pio1_sm_names);
    printf("  RXSTALL on the action SM means core 1 fell behind and bus events were lost.\n");
}

// ---- trace -------------------------------------------------------------------

#if MIA_DIAG
static void print_trace(void) {
    static mia_trace_t t;  // ~300 bytes; keep it off core 0's small stack
    mia_trace_read(&t);

    printf("Bus trace (diagnostic build):\n");
    printf("  events:%lu  ctx pushes:%lu pops:%lu overflows:%lu underflows:%lu\n",
           (unsigned long)t.events, (unsigned long)t.pushes, (unsigned long)t.pops,
           (unsigned long)t.overflows, (unsigned long)t.underflows);

    uint32_t first = (t.frozen ? t.frozen_at : t.events) - t.count;
    if (t.frozen) {
        printf("  Captured at the first ctx error (event #%lu); 'trace clear' re-arms it.\n",
               (unsigned long)t.frozen_at);
        printf("  pio0 fdebug:$%08lX  pio1 fdebug:$%08lX at the capture\n",
               (unsigned long)t.fdebug0, (unsigned long)t.fdebug1);
    } else {
        printf("  No ctx error captured; showing the latest events.\n");
    }

    for (uint32_t i = 0; i < t.count; i++) {
        uint32_t w = t.words[i];
        unsigned data = w & 0xFFu;
        unsigned offset = (w >> 8) & 0x1Fu;
        bool write = ((w >> 13) & 1u) != 0;
        printf("  #%-9lu %c $%04X %-12s %s $%02X\n",
               (unsigned long)(first + i), write ? 'W' : 'R',
               0xFFE0u + offset, reg_names[offset], write ? "<-" : "->", data);
    }
}
#endif

void con_diag_trace(const char *args) {
    args = skip_ws(args);
#if MIA_DIAG
    if (strcmp(args, "clear") == 0) {
        mia_trace_clear();
        printf("Trace: cleared and re-armed\n");
        return;
    }
    if (*args) {
        printf("Usage: trace [clear]\n");
        return;
    }
    print_trace();
#else
    (void)args;
    printf("Trace: not in this build. Build the diagnostic firmware with 'make diag'.\n");
#endif
}

// ---- dump to the console -------------------------------------------------------

static void dump_regs(void) {
    printf("Registers ($FFE0-$FFFF):\n");
    for (unsigned i = 0; i < 32; i++) {
        printf("  $%04X %-12s $%02X\n", 0xFFE0u + i, reg_names[i], (unsigned)mia_regs->bytes[i]);
    }
}

static void dump_indexes_in_use(void) {
    unsigned used = 0;
    printf("Indexes in use (any field non-zero):\n");
    for (unsigned id = 0; id < 256; id++) {
        if (!index_in_use(&idx[id])) continue;
        index_t copy = index_copy((uint8_t)id);
        print_index_line((uint8_t)id, &copy);
        used++;
    }
    printf("  %u of 256 in use\n", used);
}

static void dump_errors(void) {
    uint8_t first = _err_first;
    uint8_t count = (uint8_t)((_err_last - first) & 15u);
    printf("Error queue: %u\n", (unsigned)count);
    for (uint8_t i = 0; i < count; i++) {
        printf("  %2u: $%02X\n", (unsigned)i, (unsigned)_err_buf[(first + i) & 15u]);
    }
}

// ---- dump to a file --------------------------------------------------------------

// Snapshot format (docs/diagnostics.md): "MIASNAP" + version byte, then
// sections of a 4-character tag, a little-endian u32 length and the data,
// ending with an "END " section of length 0.
#define SNAP_VERSION 1u

static const char *snap_err;
static uint32_t snap_bytes;

static void put_u32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static void snap_write(const void *data, uint32_t length) {
    if (snap_err == NULL && length != 0) {
        snap_err = mia_sd_console_write(data, length);
        snap_bytes += length;
    }
}

static void snap_section(const char tag[4], const void *data, uint32_t length) {
    uint8_t header[8];
    memcpy(header, tag, 4);
    put_u32(&header[4], length);
    snap_write(header, sizeof(header));
    snap_write(data, length);
}

static void write_snapshot(void) {
    static uint8_t buf[512];  // largest section built here: TRCE, 296 bytes

    snap_write("MIASNAP", 7);
    uint8_t version = SNAP_VERSION;
    snap_write(&version, 1);

    // INFO: phi2_hz u32, status u16, flags u8 (bit0 normal mode, bit1 exec
    // paused, bit2 diagnostic build), reserved u8, uptime_ms u32, ram_size u32.
    uint16_t st = mia_status_get();
    memset(buf, 0, 16);
    put_u32(&buf[0], mia_applied_phi2_hz);
    buf[4] = (uint8_t)st;
    buf[5] = (uint8_t)(st >> 8);
    buf[6] = (uint8_t)(((st & MIA_STAT_MASTER_MODE) ? 1u : 0u) |
                       (mia_exec_is_paused() ? 2u : 0u) |
                       (MIA_DIAG ? 4u : 0u));
    put_u32(&buf[8], to_ms_since_boot(get_absolute_time()));
    put_u32(&buf[12], MIA_RAM_SIZE);
    snap_section("INFO", buf, 16);

    // REGS: the 32 registers, $FFE0 first.
    for (unsigned i = 0; i < 32; i++) buf[i] = mia_regs->bytes[i];
    snap_section("REGS", buf, 32);

    // IDX: 256 descriptors of 16 bytes: current, default, limit (u32 each),
    // step u16, flags u8, reserved u8.
    // Written straight from the table, which is laid out exactly so.
    snap_section("IDX ", (const void *)idx, sizeof(index_t) * 256u);

    // CTX: depth u8, then per entry (oldest first) IDXA sel, IDXB sel,
    // CFG sel, 3 params, saved IDXA descriptor (16), saved IDXB descriptor (16).
    {
        mia_ctx_t stack[MIA_CTX_DEPTH];
        uint8_t depth = mia_ctx_read(stack);
        uint32_t n = 0;
        buf[n++] = depth;
        for (uint8_t i = 0; i < depth; i++) {
            buf[n++] = stack[i].idxa_selector;
            buf[n++] = stack[i].idxb_selector;
            buf[n++] = stack[i].cfg_selector;
            memcpy(&buf[n], stack[i].cmd_param, 3); n += 3;
            memcpy(&buf[n], stack[i].idxa.bytes, 16); n += 16;
            memcpy(&buf[n], stack[i].idxb.bytes, 16); n += 16;
        }
        snap_section("CTX ", buf, n);
    }

    // ERRQ: count u8, then the queued codes, oldest first.
    {
        uint8_t first = _err_first;
        uint8_t count = (uint8_t)((_err_last - first) & 15u);
        buf[0] = count;
        for (uint8_t i = 0; i < count; i++) buf[1u + i] = _err_buf[(first + i) & 15u];
        snap_section("ERRQ", buf, 1u + count);
    }

    // PIO: pio0 fdebug, pio1 fdebug, pio0 flevel, pio1 flevel (u32 each),
    // then the PC of pio0 SM0-3 and pio1 SM0-3 (u8 each).
    put_u32(&buf[0], pio0->fdebug);
    put_u32(&buf[4], pio1->fdebug);
    put_u32(&buf[8], pio0->flevel);
    put_u32(&buf[12], pio1->flevel);
    for (unsigned sm = 0; sm < 4; sm++) {
        buf[16 + sm] = (uint8_t)(pio0->sm[sm].addr & 0x1Fu);
        buf[20 + sm] = (uint8_t)(pio1->sm[sm].addr & 0x1Fu);
    }
    snap_section("PIO ", buf, 24);

#if MIA_DIAG
    // TRCE (diagnostic build only): events, pushes, pops, overflows,
    // underflows, frozen (0/1), frozen_at, fdebug0, fdebug1, count (u32 each),
    // then count raw action words, oldest first.
    {
        static mia_trace_t t;
        mia_trace_read(&t);
        uint32_t fields[10] = {
            t.events, t.pushes, t.pops, t.overflows, t.underflows,
            t.frozen ? 1u : 0u, t.frozen_at, t.fdebug0, t.fdebug1, t.count,
        };
        for (unsigned i = 0; i < 10; i++) put_u32(&buf[i * 4u], fields[i]);
        for (uint32_t i = 0; i < t.count; i++) put_u32(&buf[40u + i * 4u], t.words[i]);
        snap_section("TRCE", buf, 40u + t.count * 4u);
    }
#endif

    // RAM: all of MIA RAM, straight from memory.
    snap_section("RAM ", mem, MIA_RAM_SIZE);
    snap_section("END ", NULL, 0);
}

static bool open_file(const char *path) {
    const char *err = mia_sd_console_open(path);
    if (err != NULL) {
        printf("Dump: %s\n", err);
        return false;
    }
    return true;
}

static void close_file(const char *path, uint32_t bytes, const char *write_err) {
    const char *close_err = mia_sd_console_close();
    if (write_err == NULL) write_err = close_err;
    if (write_err != NULL) {
        printf("Dump: %s: %s\n", path, write_err);
        return;
    }
    printf("Dump: wrote %lu bytes to %s\n", (unsigned long)bytes, path);
}

// ---- dump --------------------------------------------------------------------

static void dump_usage(void) {
    printf("Usage: dump regs | idx [id] | ram ADDR LEN [FILE] | all [FILE]\n");
    printf("  ADDR and LEN are hex. With FILE, ram writes the raw bytes and all\n");
    printf("  writes a snapshot (docs/diagnostics.md) to the SD card.\n");
}

void con_diag_dump(const char *args) {
    char what[8];
    char a1[16];
    char a2[16];
    char file[MIA_FS_PATH_SIZE];

    if (!next_word(&args, what, sizeof(what))) {
        dump_usage();
        return;
    }

    if (strcmp(what, "regs") == 0) {
        if (*skip_ws(args)) { dump_usage(); return; }
        dump_regs();
        return;
    }

    if (strcmp(what, "idx") == 0) {
        if (!next_word(&args, a1, sizeof(a1))) {
            dump_indexes_in_use();
            return;
        }
        uint8_t id;
        if (!parse_index_id(a1, &id) || *skip_ws(args)) {
            printf("Usage: dump idx [id]  (id: 0-255 or $00-$FF)\n");
            return;
        }
        index_t copy = index_copy(id);
        print_index_line(id, &copy);
        return;
    }

    if (strcmp(what, "ram") == 0) {
        uint32_t addr, len;
        if (!next_word(&args, a1, sizeof(a1)) || !parse_hex(a1, &addr) ||
            !next_word(&args, a2, sizeof(a2)) || !parse_hex(a2, &len) || len == 0) {
            printf("Usage: dump ram ADDR LEN [FILE]  (hex, e.g. dump ram 14000 100)\n");
            return;
        }
        if (addr >= MIA_RAM_SIZE) {
            printf("Dump: address out of range (max $%05X)\n", MIA_RAM_SIZE - 1);
            return;
        }
        if (len > MIA_RAM_SIZE - addr) {
            len = MIA_RAM_SIZE - addr;
        }

        if (!next_word(&args, file, sizeof(file))) {
            monitor_dump(addr, len);
            return;
        }
        if (*skip_ws(args)) { dump_usage(); return; }
        if (!open_file(file)) return;
        print_running_note();
        close_file(file, len, mia_sd_console_write(&mem[addr], len));
        return;
    }

    if (strcmp(what, "all") == 0) {
        if (!next_word(&args, file, sizeof(file))) {
            dump_regs();
            con_diag_status_ctx();
            con_diag_status_pio("");
            dump_errors();
            dump_indexes_in_use();
#if MIA_DIAG
            print_trace();
#endif
            return;
        }
        if (*skip_ws(args)) { dump_usage(); return; }
        if (!open_file(file)) return;
        print_running_note();
        printf("Dump: writing snapshot...\n");
        snap_err = NULL;
        snap_bytes = 0;
        write_snapshot();
        close_file(file, snap_bytes, snap_err);
        return;
    }

    dump_usage();
}
