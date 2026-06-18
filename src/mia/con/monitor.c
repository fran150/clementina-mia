#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#include "monitor.h"
#include "mem/mem.h"
#include "video/video_dirty.h"

// ---- 65C02 disassembler --------------------------------------------------

typedef enum {
    AM_IMP,  // Implied
    AM_ACC,  // Accumulator        A
    AM_IMM,  // Immediate          #$XX
    AM_ZPG,  // Zero Page          $XX
    AM_ZPX,  // Zero Page,X        $XX,X
    AM_ZPY,  // Zero Page,Y        $XX,Y
    AM_ABS,  // Absolute           $XXXX
    AM_ABX,  // Absolute,X         $XXXX,X
    AM_ABY,  // Absolute,Y         $XXXX,Y
    AM_IND,  // Indirect           ($XXXX)
    AM_IZX,  // (Indirect,X)       ($XX,X)
    AM_IZY,  // (Indirect),Y       ($XX),Y
    AM_REL,  // Relative           branch -> absolute target
    AM_ZPI,  // ZP Indirect        ($XX)          65C02
    AM_AIX,  // (Absolute,X)       ($XXXX,X)      65C02
    AM_ZPR,  // ZP + Relative      $XX,$XXXX      65C02 Rockwell
} addr_mode_t;

static const uint8_t mode_size[] = {
    [AM_IMP] = 1, [AM_ACC] = 1, [AM_IMM] = 2, [AM_ZPG] = 2,
    [AM_ZPX] = 2, [AM_ZPY] = 2, [AM_ABS] = 3, [AM_ABX] = 3,
    [AM_ABY] = 3, [AM_IND] = 3, [AM_IZX] = 2, [AM_IZY] = 2,
    [AM_REL] = 2, [AM_ZPI] = 2, [AM_AIX] = 3, [AM_ZPR] = 3,
};

static const struct { const char *mnem; addr_mode_t mode; } opcodes[256] = {
/* 00 */ {"BRK",AM_IMP},{"ORA",AM_IZX},{"???",AM_IMM},{"???",AM_IMP},
         {"TSB",AM_ZPG},{"ORA",AM_ZPG},{"ASL",AM_ZPG},{"RMB0",AM_ZPG},
         {"PHP",AM_IMP},{"ORA",AM_IMM},{"ASL",AM_ACC},{"???",AM_IMP},
         {"TSB",AM_ABS},{"ORA",AM_ABS},{"ASL",AM_ABS},{"BBR0",AM_ZPR},
/* 10 */ {"BPL",AM_REL},{"ORA",AM_IZY},{"ORA",AM_ZPI},{"???",AM_IMP},
         {"TRB",AM_ZPG},{"ORA",AM_ZPX},{"ASL",AM_ZPX},{"RMB1",AM_ZPG},
         {"CLC",AM_IMP},{"ORA",AM_ABY},{"INC",AM_ACC},{"???",AM_IMP},
         {"TRB",AM_ABS},{"ORA",AM_ABX},{"ASL",AM_ABX},{"BBR1",AM_ZPR},
/* 20 */ {"JSR",AM_ABS},{"AND",AM_IZX},{"???",AM_IMM},{"???",AM_IMP},
         {"BIT",AM_ZPG},{"AND",AM_ZPG},{"ROL",AM_ZPG},{"RMB2",AM_ZPG},
         {"PLP",AM_IMP},{"AND",AM_IMM},{"ROL",AM_ACC},{"???",AM_IMP},
         {"BIT",AM_ABS},{"AND",AM_ABS},{"ROL",AM_ABS},{"BBR2",AM_ZPR},
/* 30 */ {"BMI",AM_REL},{"AND",AM_IZY},{"AND",AM_ZPI},{"???",AM_IMP},
         {"BIT",AM_ZPX},{"AND",AM_ZPX},{"ROL",AM_ZPX},{"RMB3",AM_ZPG},
         {"SEC",AM_IMP},{"AND",AM_ABY},{"DEC",AM_ACC},{"???",AM_IMP},
         {"BIT",AM_ABX},{"AND",AM_ABX},{"ROL",AM_ABX},{"BBR3",AM_ZPR},
/* 40 */ {"RTI",AM_IMP},{"EOR",AM_IZX},{"???",AM_IMM},{"???",AM_IMP},
         {"???",AM_ZPG},{"EOR",AM_ZPG},{"LSR",AM_ZPG},{"RMB4",AM_ZPG},
         {"PHA",AM_IMP},{"EOR",AM_IMM},{"LSR",AM_ACC},{"???",AM_IMP},
         {"JMP",AM_ABS},{"EOR",AM_ABS},{"LSR",AM_ABS},{"BBR4",AM_ZPR},
/* 50 */ {"BVC",AM_REL},{"EOR",AM_IZY},{"EOR",AM_ZPI},{"???",AM_IMP},
         {"???",AM_ZPX},{"EOR",AM_ZPX},{"LSR",AM_ZPX},{"RMB5",AM_ZPG},
         {"CLI",AM_IMP},{"EOR",AM_ABY},{"PHY",AM_IMP},{"???",AM_IMP},
         {"???",AM_ABS},{"EOR",AM_ABX},{"LSR",AM_ABX},{"BBR5",AM_ZPR},
/* 60 */ {"RTS",AM_IMP},{"ADC",AM_IZX},{"???",AM_IMM},{"???",AM_IMP},
         {"STZ",AM_ZPG},{"ADC",AM_ZPG},{"ROR",AM_ZPG},{"RMB6",AM_ZPG},
         {"PLA",AM_IMP},{"ADC",AM_IMM},{"ROR",AM_ACC},{"???",AM_IMP},
         {"JMP",AM_IND},{"ADC",AM_ABS},{"ROR",AM_ABS},{"BBR6",AM_ZPR},
/* 70 */ {"BVS",AM_REL},{"ADC",AM_IZY},{"ADC",AM_ZPI},{"???",AM_IMP},
         {"STZ",AM_ZPX},{"ADC",AM_ZPX},{"ROR",AM_ZPX},{"RMB7",AM_ZPG},
         {"SEI",AM_IMP},{"ADC",AM_ABY},{"PLY",AM_IMP},{"???",AM_IMP},
         {"JMP",AM_AIX},{"ADC",AM_ABX},{"ROR",AM_ABX},{"BBR7",AM_ZPR},
/* 80 */ {"BRA",AM_REL},{"STA",AM_IZX},{"???",AM_IMM},{"???",AM_IMP},
         {"STY",AM_ZPG},{"STA",AM_ZPG},{"STX",AM_ZPG},{"SMB0",AM_ZPG},
         {"DEY",AM_IMP},{"BIT",AM_IMM},{"TXA",AM_IMP},{"???",AM_IMP},
         {"STY",AM_ABS},{"STA",AM_ABS},{"STX",AM_ABS},{"BBS0",AM_ZPR},
/* 90 */ {"BCC",AM_REL},{"STA",AM_IZY},{"STA",AM_ZPI},{"???",AM_IMP},
         {"STY",AM_ZPX},{"STA",AM_ZPX},{"STX",AM_ZPY},{"SMB1",AM_ZPG},
         {"TYA",AM_IMP},{"STA",AM_ABY},{"TXS",AM_IMP},{"???",AM_IMP},
         {"STZ",AM_ABS},{"STA",AM_ABX},{"STZ",AM_ABX},{"BBS1",AM_ZPR},
/* A0 */ {"LDY",AM_IMM},{"LDA",AM_IZX},{"LDX",AM_IMM},{"???",AM_IMP},
         {"LDY",AM_ZPG},{"LDA",AM_ZPG},{"LDX",AM_ZPG},{"SMB2",AM_ZPG},
         {"TAY",AM_IMP},{"LDA",AM_IMM},{"TAX",AM_IMP},{"???",AM_IMP},
         {"LDY",AM_ABS},{"LDA",AM_ABS},{"LDX",AM_ABS},{"BBS2",AM_ZPR},
/* B0 */ {"BCS",AM_REL},{"LDA",AM_IZY},{"LDA",AM_ZPI},{"???",AM_IMP},
         {"LDY",AM_ZPX},{"LDA",AM_ZPX},{"LDX",AM_ZPY},{"SMB3",AM_ZPG},
         {"CLV",AM_IMP},{"LDA",AM_ABY},{"TSX",AM_IMP},{"???",AM_IMP},
         {"LDY",AM_ABX},{"LDA",AM_ABX},{"LDX",AM_ABY},{"BBS3",AM_ZPR},
/* C0 */ {"CPY",AM_IMM},{"CMP",AM_IZX},{"???",AM_IMM},{"???",AM_IMP},
         {"CPY",AM_ZPG},{"CMP",AM_ZPG},{"DEC",AM_ZPG},{"SMB4",AM_ZPG},
         {"INY",AM_IMP},{"CMP",AM_IMM},{"DEX",AM_IMP},{"WAI",AM_IMP},
         {"CPY",AM_ABS},{"CMP",AM_ABS},{"DEC",AM_ABS},{"BBS4",AM_ZPR},
/* D0 */ {"BNE",AM_REL},{"CMP",AM_IZY},{"CMP",AM_ZPI},{"???",AM_IMP},
         {"???",AM_ZPX},{"CMP",AM_ZPX},{"DEC",AM_ZPX},{"SMB5",AM_ZPG},
         {"CLD",AM_IMP},{"CMP",AM_ABY},{"PHX",AM_IMP},{"STP",AM_IMP},
         {"???",AM_ABS},{"CMP",AM_ABX},{"DEC",AM_ABX},{"BBS5",AM_ZPR},
/* E0 */ {"CPX",AM_IMM},{"SBC",AM_IZX},{"???",AM_IMM},{"???",AM_IMP},
         {"CPX",AM_ZPG},{"SBC",AM_ZPG},{"INC",AM_ZPG},{"SMB6",AM_ZPG},
         {"INX",AM_IMP},{"SBC",AM_IMM},{"NOP",AM_IMP},{"???",AM_IMP},
         {"CPX",AM_ABS},{"SBC",AM_ABS},{"INC",AM_ABS},{"BBS6",AM_ZPR},
/* F0 */ {"BEQ",AM_REL},{"SBC",AM_IZY},{"SBC",AM_ZPI},{"???",AM_IMP},
         {"???",AM_ZPX},{"SBC",AM_ZPX},{"INC",AM_ZPX},{"SMB7",AM_ZPG},
         {"SED",AM_IMP},{"SBC",AM_ABY},{"PLX",AM_IMP},{"???",AM_IMP},
         {"???",AM_ABS},{"SBC",AM_ABX},{"INC",AM_ABX},{"BBS7",AM_ZPR},
};

static uint32_t disassemble_one(uint32_t addr) {
    if (addr >= MIA_RAM_SIZE) {
        printf("$%05lX: [out of range]\n", (unsigned long)addr);
        return addr + 1;
    }

    uint8_t opcode = mem[addr];
    const char *mnem = opcodes[opcode].mnem;
    addr_mode_t mode = opcodes[opcode].mode;
    uint8_t size = mode_size[mode];

    uint8_t op1 = (size >= 2 && addr + 1 < MIA_RAM_SIZE) ? mem[addr + 1] : 0;
    uint8_t op2 = (size >= 3 && addr + 2 < MIA_RAM_SIZE) ? mem[addr + 2] : 0;
    uint16_t word = (uint16_t)(op1 | ((uint16_t)op2 << 8));

    printf("$%05lX: ", (unsigned long)addr);

    for (int i = 0; i < 3; i++) {
        if (i < size) {
            uint8_t b = (i == 0) ? opcode : (i == 1) ? op1 : op2;
            printf("%02X ", b);
        } else {
            printf("   ");
        }
    }

    printf("%-5s", mnem);

    switch (mode) {
        case AM_IMP: break;
        case AM_ACC: printf("A"); break;
        case AM_IMM: printf("#$%02X", op1); break;
        case AM_ZPG: printf("$%02X", op1); break;
        case AM_ZPX: printf("$%02X,X", op1); break;
        case AM_ZPY: printf("$%02X,Y", op1); break;
        case AM_ABS: printf("$%04X", word); break;
        case AM_ABX: printf("$%04X,X", word); break;
        case AM_ABY: printf("$%04X,Y", word); break;
        case AM_IND: printf("($%04X)", word); break;
        case AM_IZX: printf("($%02X,X)", op1); break;
        case AM_IZY: printf("($%02X),Y", op1); break;
        case AM_REL: {
            uint16_t target = (uint16_t)((uint16_t)(addr + 2) + (int8_t)op1);
            printf("$%04X", target);
            break;
        }
        case AM_ZPI: printf("($%02X)", op1); break;
        case AM_AIX: printf("($%04X,X)", word); break;
        case AM_ZPR: {
            uint16_t target = (uint16_t)((uint16_t)(addr + 3) + (int8_t)op2);
            printf("$%02X,$%04X", op1, target);
            break;
        }
    }

    printf("\n");
    return addr + size;
}

// ---- Public API ----------------------------------------------------------

void monitor_dump(uint32_t addr, uint32_t len) {
    if (addr >= MIA_RAM_SIZE || len == 0) return;

    uint32_t end = addr + len;
    if (end > MIA_RAM_SIZE) end = MIA_RAM_SIZE;

    uint32_t row_start = addr & ~0xFu;

    for (uint32_t row = row_start; row < end; row += 16) {
        printf("$%05lX: ", (unsigned long)row);

        for (int i = 0; i < 16; i++) {
            if (i == 8) printf(" ");
            uint32_t a = row + (uint32_t)i;
            if (a < addr || a >= end) printf("   ");
            else                      printf("%02X ", mem[a]);
        }

        printf(" ");
        for (int i = 0; i < 16; i++) {
            uint32_t a = row + (uint32_t)i;
            if (a < addr || a >= end) {
                printf(" ");
            } else {
                uint8_t c = mem[a];
                printf("%c", (c >= 0x20 && c < 0x7F) ? (char)c : '.');
            }
        }
        printf("\n");
    }
}

uint32_t monitor_disassemble(uint32_t addr, uint32_t count) {
    for (uint32_t i = 0; i < count; i++) {
        if (addr >= MIA_RAM_SIZE) break;
        addr = disassemble_one(addr);
    }
    return addr;
}

void monitor_poke(uint32_t addr, const uint8_t *bytes, uint32_t count) {
    if (addr >= MIA_RAM_SIZE) {
        return;
    }

    uint32_t writable = MIA_RAM_SIZE - addr;
    if (count > writable) {
        count = writable;
    }

    for (uint32_t i = 0; i < count; i++) {
        mem[addr + i] = bytes[i];
    }
    mia_video_mark_dirty_range(addr, count);
}

// ---- Interactive command dispatch ----------------------------------------

#define MON_DEFAULT_DUMP   128
#define MON_DEFAULT_DISASM 16

static uint32_t last_dump_addr   = 0;
static uint32_t last_disasm_addr = 0;

static const char *skip_ws(const char *p) {
    while (*p == ' ' || *p == '\t') p++;
    return p;
}

static bool next_hex(const char **p, uint32_t *out) {
    const char *s = skip_ws(*p);
    if (*s == '$') s++;
    if (!*s) return false;

    uint32_t val = 0;
    bool found = false;
    while (1) {
        char c = *s;
        if      (c >= '0' && c <= '9') { val = (val << 4) | (uint32_t)(c - '0');      }
        else if (c >= 'a' && c <= 'f') { val = (val << 4) | (uint32_t)(c - 'a' + 10); }
        else if (c >= 'A' && c <= 'F') { val = (val << 4) | (uint32_t)(c - 'A' + 10); }
        else break;
        s++;
        found = true;
    }

    if (!found) return false;
    *out = val;
    *p = s;
    return true;
}

static void print_help(void) {
    printf("  m [ADDR [LEN]]    Dump memory, hex+ASCII (default %d bytes)\n", MON_DEFAULT_DUMP);
    printf("  u [ADDR [COUNT]]  Disassemble 65C02 (default %d instructions)\n", MON_DEFAULT_DISASM);
    printf("  e ADDR BYTE...    Edit memory (space-separated hex bytes)\n");
    printf("  ? / help          Show this help\n");
    printf("  quit              Return to console\n");
}

void monitor_print_banner(void) {
    printf("\n65C02 Monitor  [MIA RAM: %uKB, $00000-$%05X]\n",
           MIA_RAM_SIZE / 1024, MIA_RAM_SIZE - 1);
    print_help();
    printf("\n");
}

bool monitor_exec_line(const char *line) {
    const char *p = skip_ws(line);
    if (!*p) return true;

    char cmd[8] = {0};
    int ci = 0;
    while (*p && *p != ' ' && *p != '\t' && ci < 7) {
        char c = *p++;
        cmd[ci++] = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
    }

    if (strcmp(cmd, "quit") == 0) return false;

    if (strcmp(cmd, "?") == 0 || strcmp(cmd, "help") == 0) {
        print_help();
        return true;
    }

    if (strcmp(cmd, "m") == 0) {
        uint32_t addr = last_dump_addr, len = MON_DEFAULT_DUMP;
        next_hex(&p, &addr);
        next_hex(&p, &len);
        if (addr >= MIA_RAM_SIZE) { printf("Address out of range (max $%05X)\n", MIA_RAM_SIZE - 1); return true; }
        monitor_dump(addr, len);
        last_dump_addr = addr + len;
        return true;
    }

    if (strcmp(cmd, "u") == 0) {
        uint32_t addr = last_disasm_addr, count = MON_DEFAULT_DISASM;
        next_hex(&p, &addr);
        next_hex(&p, &count);
        if (addr >= MIA_RAM_SIZE) { printf("Address out of range (max $%05X)\n", MIA_RAM_SIZE - 1); return true; }
        last_disasm_addr = monitor_disassemble(addr, count);
        return true;
    }

    if (strcmp(cmd, "e") == 0) {
        uint32_t addr;
        if (!next_hex(&p, &addr)) { printf("Usage: e ADDR BYTE [BYTE ...]\n"); return true; }
        uint32_t cur = addr, val;
        while (next_hex(&p, &val)) {
            if (cur >= MIA_RAM_SIZE) { printf("Address overflow at $%05lX\n", (unsigned long)cur); break; }
            uint8_t b = (uint8_t)val;
            monitor_poke(cur++, &b, 1);
        }
        if (cur == addr) printf("Usage: e ADDR BYTE [BYTE ...]\n");
        return true;
    }

    printf("Unknown command '%s'. Type ? for help.\n", cmd);
    return true;
}
