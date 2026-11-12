#ifndef _MIA_SYS_MIA_H_
#define _MIA_SYS_MIA_H_

#include <stdbool.h>
#include <stdint.h>

#include "mem/indexes.h"

void mia_init(void);
void mia_service(void);

// One saved MIA_CTX entry (see regs.h): what a push stores and a pop restores.
typedef struct {
    uint8_t idxa_selector;
    uint8_t idxb_selector;
    uint8_t cfg_selector;
    uint8_t cmd_param[3];
    index_t idxa;
    index_t idxb;
} mia_ctx_t;

// Copies the MIA_CTX stack for the console, oldest entry first, and returns
// its depth. Core 1 owns the stack, so a copy taken while the 6502 runs can
// be torn; pause execution first for an exact copy. out holds MIA_CTX_DEPTH.
uint8_t mia_ctx_read(mia_ctx_t *out);

// Bus event trace, only in the diagnostic build (make diag). Normal firmware
// builds with MIA_DIAG 0 and its action loop has no trace code at all.
#ifndef MIA_DIAG
#define MIA_DIAG 0
#endif

#if MIA_DIAG
#define MIA_TRACE_RING 64u  // power of two

typedef struct {
    uint32_t events;     // action events seen since the last clear
    uint32_t pushes;     // MIA_CTX pushes
    uint32_t pops;       // MIA_CTX pops
    uint32_t overflows;  // pushes on a full stack
    uint32_t underflows; // pops on an empty stack
    bool frozen;         // words[] is the capture taken at the first ctx error
    uint32_t frozen_at;  // events count when the capture was taken
    uint32_t fdebug0;    // pio0 FDEBUG at the capture
    uint32_t fdebug1;    // pio1 FDEBUG at the capture
    uint32_t count;      // valid entries in words[]
    // Raw action words, oldest first: CS | R/W | 5 address bits | 8 data bits.
    uint32_t words[MIA_TRACE_RING];
} mia_trace_t;

// Fills out with the frozen capture if there is one, else with the last
// events seen so far.
void mia_trace_read(mia_trace_t *out);

// Zeroes the counters, drops the capture and re-arms it.
void mia_trace_clear(void);

// Core 1 load for `status sys`: the cycles core 1 has spent on loop passes
// that handled a bus event, and the bus events seen. Both only count up and
// wrap; take differences. Not reset by mia_trace_clear.
void mia_core1_load_read(uint32_t *busy_cycles, uint32_t *events);
#endif

#endif
