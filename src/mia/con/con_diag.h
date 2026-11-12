#ifndef _MIA_CON_CON_DIAG_H_
#define _MIA_CON_CON_DIAG_H_

// Diagnostic console commands (see docs/diagnostics.md). They all run on
// core 0 and only read MIA state, except `status pio clear` and
// `trace clear`, which reset sticky diagnostic flags and counters.

// status ctx: the MIA_CTX stack.
void con_diag_status_ctx(void);

// status pio [clear]: bus PIO stall/underrun flags, FIFO levels, SM PCs.
void con_diag_status_pio(const char *args);

// dump regs | idx [id] | ram ADDR LEN [FILE] | all [FILE]
void con_diag_dump(const char *args);

// trace [clear]: the bus event trace (diagnostic build only).
void con_diag_trace(const char *args);

#endif
