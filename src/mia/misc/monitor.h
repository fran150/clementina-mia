#ifndef _MIA_MISC_MONITOR_H_
#define _MIA_MISC_MONITOR_H_

#include <stdbool.h>
#include <stdint.h>

// Dump len bytes of MIA RAM starting at addr in hex+ASCII format.
void monitor_dump(uint32_t addr, uint32_t len);

// Disassemble count 65C02 instructions from addr.
// Returns the address immediately after the last disassembled instruction.
uint32_t monitor_disassemble(uint32_t addr, uint32_t count);

// Write bytes into MIA RAM at addr, marking video dirty as needed.
void monitor_poke(uint32_t addr, const uint8_t *bytes, uint32_t count);

// Print the monitor welcome banner and command summary.
void monitor_print_banner(void);

// Execute one monitor command line.
// Returns false when the user types quit (signals exit from monitor mode).
bool monitor_exec_line(const char *line);

#endif
