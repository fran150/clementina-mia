#include "sysmon.h"

#include <malloc.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "hardware/adc.h"
#include "hardware/clocks.h"
#include "hardware/pio.h"
#include "hardware/structs/m33.h"
#include "hardware/structs/powman.h"
#include "hardware/structs/timer.h"

#include "hardware/pio_mapping.h"
#include "net/wifi.h"
#include "sys/mia.h"

// Linker symbols (pico-sdk memmap): the end of the program image in flash,
// the end of static data in SRAM, the heap and both stacks.
extern char __flash_binary_end;
extern char __bss_end__;
extern char __end__;
extern char __HeapLimit;
extern uint32_t __StackBottom;
extern uint32_t __StackTop;
extern uint32_t __StackOneBottom;
extern uint32_t __StackOneTop;

#define STACK_PAINT 0x5354434Bu  // "STCK"
#define WINDOW_US   1000000u

static const char *const slot_names[SYSMON_SLOTS] = {
    "mia", "sd", "wifi", "video", "console", "other"
};

// Current lap and window.
static uint32_t lap_cycles_start;
static uint32_t lap_us_start;
static uint32_t pass_us_start;
static uint32_t window_us_start;
static uint32_t window_passes;
static uint64_t window_cycles[SYSMON_SLOTS];
static uint32_t cycles_per_us;

// The last complete window, shown by `status sys`.
static uint64_t last_cycles[SYSMON_SLOTS];
static uint64_t last_total;
static uint32_t last_us;
static uint32_t last_passes;

// Peaks since boot or `status sys clear`.
static uint32_t slot_max_us[SYSMON_SLOTS];
static uint32_t pass_max_us;
static uint8_t act_fifo_peak;

#if MIA_DIAG
static uint32_t c1_busy_start;
static uint32_t c1_events_start;
static uint32_t c1_busy_last;
static uint32_t c1_events_last;
#endif

static bool adc_ready;

static inline uint32_t cycles_now(void) {
    return m33_hw->dwt_cyccnt;
}

static inline uint32_t us_now(void) {
    return timer_hw->timerawl;
}

static void paint(uint32_t *from, uint32_t *to) {
    for (uint32_t *p = from; p < to; p++) {
        *p = STACK_PAINT;
    }
}

// Bytes between the top of the stack and its deepest written word.
static uint32_t stack_peak(const uint32_t *bottom, const uint32_t *top) {
    const uint32_t *p = bottom;
    while (p < top && *p == STACK_PAINT) {
        p++;
    }
    return (uint32_t)((const char *)top - (const char *)p);
}

void sysmon_init(void) {
    m33_hw->demcr |= M33_DEMCR_TRCENA_BITS;
    m33_hw->dwt_cyccnt = 0;
    m33_hw->dwt_ctrl |= M33_DWT_CTRL_CYCCNTENA_BITS;

    // Core 0 is running on its stack: paint only below the current frame.
    uint32_t marker;
    uint32_t *below_frame = (uint32_t *)((uintptr_t)&marker & ~3u) - 16;
    paint(&__StackBottom, below_frame);
    // Core 1 has not started yet.
    paint(&__StackOneBottom, &__StackOneTop);

    cycles_per_us = clock_get_hz(clk_sys) / 1000000u;
    lap_cycles_start = cycles_now();
    lap_us_start = pass_us_start = window_us_start = us_now();
}

void __not_in_flash_func(sysmon_lap)(sysmon_slot_t slot) {
    uint32_t c = cycles_now();
    uint32_t u = us_now();
    uint32_t lap_us = u - lap_us_start;
    uint64_t lap_cycles = c - lap_cycles_start;

    // The cycle counter wraps after 2^32 cycles (about 16 s at 256 MHz); a
    // lap that long (a Wi-Fi connect, say) is counted from the microsecond timer.
    if (lap_us > WINDOW_US) {
        lap_cycles = (uint64_t)lap_us * cycles_per_us;
    }

    window_cycles[slot] += lap_cycles;
    if (lap_us > slot_max_us[slot]) {
        slot_max_us[slot] = lap_us;
    }
    lap_cycles_start = c;
    lap_us_start = u;
}

static void window_close(uint32_t now) {
    uint64_t total = 0;
    for (int i = 0; i < SYSMON_SLOTS; i++) {
        last_cycles[i] = window_cycles[i];
        total += window_cycles[i];
        window_cycles[i] = 0;
    }
    last_total = total;
    last_us = now - window_us_start;
    last_passes = window_passes;
    window_passes = 0;
    window_us_start = now;

#if MIA_DIAG
    uint32_t busy, events;
    mia_core1_load_read(&busy, &events);
    c1_busy_last = busy - c1_busy_start;
    c1_events_last = events - c1_events_start;
    c1_busy_start = busy;
    c1_events_start = events;
#endif

    // `speed` may have changed the system clock.
    cycles_per_us = clock_get_hz(clk_sys) / 1000000u;
}

void __not_in_flash_func(sysmon_pass_end)(void) {
    sysmon_lap(SYSMON_OTHER);

    uint32_t now = lap_us_start;
    uint32_t pass_us = now - pass_us_start;
    if (pass_us > pass_max_us) {
        pass_max_us = pass_us;
    }
    pass_us_start = now;
    window_passes++;

    // Bus events waiting for core 1 right now. Four means the FIFO is full
    // and the next event stalls the action state machine.
    uint8_t level = (uint8_t)((MIA_ACT_PIO->flevel >> (MIA_ACT_SM * 8u + 4u)) & 0xFu);
    if (level > act_fifo_peak) {
        act_fifo_peak = level;
    }

    if (now - window_us_start >= WINDOW_US) {
        window_close(now);
    }
}

void sysmon_clear(void) {
    memset(slot_max_us, 0, sizeof(slot_max_us));
    pass_max_us = 0;
    act_fifo_peak = 0;
}

static void print_kb_line(const char *label, uint32_t used, uint32_t total, const char *note) {
    printf("  %-9s %lu KB of %lu KB (%lu%%)%s\n", label,
           (unsigned long)((used + 512u) / 1024u),
           (unsigned long)(total / 1024u),
           (unsigned long)(total ? (uint64_t)used * 100u / total : 0u),
           note);
}

static void print_reset_reason(void) {
    static const struct { uint32_t bit; const char *name; } reasons[] = {
        { POWMAN_CHIP_RESET_HAD_POR_BITS,                      "power-on" },
        { POWMAN_CHIP_RESET_HAD_BOR_BITS,                      "brown-out" },
        { POWMAN_CHIP_RESET_HAD_RUN_LOW_BITS,                  "RUN pin" },
        { POWMAN_CHIP_RESET_HAD_DP_RESET_REQ_BITS,             "debugger" },
        { POWMAN_CHIP_RESET_HAD_RESCUE_BITS,                   "rescue" },
        { POWMAN_CHIP_RESET_HAD_SWCORE_PD_BITS,                "core power-down" },
        { POWMAN_CHIP_RESET_HAD_GLITCH_DETECT_BITS,            "glitch detector" },
        { POWMAN_CHIP_RESET_HAD_HZD_SYS_RESET_REQ_BITS,        "software" },
        { POWMAN_CHIP_RESET_HAD_WATCHDOG_RESET_RSM_BITS,       "watchdog" },
        { POWMAN_CHIP_RESET_HAD_WATCHDOG_RESET_SWCORE_BITS,    "watchdog" },
        { POWMAN_CHIP_RESET_HAD_WATCHDOG_RESET_POWMAN_BITS,    "watchdog" },
        { POWMAN_CHIP_RESET_HAD_WATCHDOG_RESET_POWMAN_ASYNC_BITS, "watchdog" },
    };
    uint32_t flags = powman_hw->chip_reset;
    const char *shown = NULL;

    printf("  reset:    ");
    for (size_t i = 0; i < sizeof(reasons) / sizeof(reasons[0]); i++) {
        if ((flags & reasons[i].bit) && reasons[i].name != shown) {
            printf("%s%s", shown ? ", " : "", reasons[i].name);
            shown = reasons[i].name;
        }
    }
    printf("%s\n", shown ? "" : "unknown");
}

static void print_temperature(void) {
    if (!adc_ready) {
        // Only the internal sensor is used; no GPIO is switched to the ADC.
        adc_init();
        adc_set_temp_sensor_enabled(true);
        adc_ready = true;
    }
    adc_select_input(ADC_TEMPERATURE_CHANNEL_NUM);
    uint16_t raw = adc_read();
    // RP2350 datasheet: T = 27 - (V - 0.706) / 0.001721, V on a 3.3 V reference.
    int32_t mv = (int32_t)raw * 3300 / 4096;
    int32_t tenths = 270 - (mv - 706) * 10000 / 1721;
    printf("  temp:     %ld.%ld C (chip sensor, about +/-2 C)\n",
           (long)(tenths / 10), (long)(tenths < 0 ? -tenths % 10 : tenths % 10));
}

static void print_us(uint32_t us) {
    if (us == 0) {
        printf("%7s", "<1 us");
    } else if (us < 10000u) {
        printf("%4lu us", (unsigned long)us);
    } else {
        printf("%4lu ms", (unsigned long)(us / 1000u));
    }
}

void sysmon_print(void) {
    uint32_t ms = to_ms_since_boot(get_absolute_time());
    uint32_t s = ms / 1000u;

    printf("System:\n");
    printf("  uptime:   %lud %02lu:%02lu:%02lu\n",
           (unsigned long)(s / 86400u), (unsigned long)(s / 3600u % 24u),
           (unsigned long)(s / 60u % 60u), (unsigned long)(s % 60u));
    print_reset_reason();
    printf("  clock:    %lu MHz\n", (unsigned long)(clock_get_hz(clk_sys) / 1000000u));
    print_temperature();

    int32_t rssi;
    if (mia_net_wifi_rssi(&rssi)) {
        printf("  wifi:     %ld dBm\n", (long)rssi);
    }

    // Flash and SRAM.
    uint32_t flash_used = (uint32_t)((uintptr_t)&__flash_binary_end - XIP_BASE);
    print_kb_line("flash:", flash_used, PICO_FLASH_SIZE_BYTES, "  firmware");

    uint32_t sram_total = SRAM_END - SRAM_BASE;
    uint32_t static_used = (uint32_t)((uintptr_t)&__bss_end__ - SRAM_BASE);
    print_kb_line("RAM:", static_used, sram_total, "  static, 256 KB of it MIA RAM");

    struct mallinfo mi = mallinfo();
    uint32_t heap_total = (uint32_t)(&__HeapLimit - &__end__);
    uint32_t heap_free = heap_total - (uint32_t)mi.arena + (uint32_t)mi.fordblks;
    printf("  heap:     %lu bytes in use, %lu KB free\n",
           (unsigned long)mi.uordblks, (unsigned long)(heap_free / 1024u));

    uint32_t c0_size = (uint32_t)((char *)&__StackTop - (char *)&__StackBottom);
    uint32_t c1_size = (uint32_t)((char *)&__StackOneTop - (char *)&__StackOneBottom);
    uint32_t c0_peak = stack_peak(&__StackBottom, &__StackTop);
    uint32_t c1_peak = stack_peak(&__StackOneBottom, &__StackOneTop);
    printf("  stacks:   core 0 %lu of %lu bytes%s, core 1 %lu of %lu bytes%s (deepest since boot)\n",
           (unsigned long)c0_peak, (unsigned long)c0_size, c0_peak >= c0_size ? " FULL" : "",
           (unsigned long)c1_peak, (unsigned long)c1_size, c1_peak >= c1_size ? " FULL" : "");

    // Core 0.
    printf("Core 0 main loop (share of the last %lu ms, slowest since clear):\n",
           (unsigned long)(last_us / 1000u));
    if (last_total == 0) {
        printf("  (no complete window yet)\n");
    } else {
        for (int i = 0; i < SYSMON_SLOTS; i++) {
            uint32_t tenths = (uint32_t)(last_cycles[i] * 1000u / last_total);
            printf("  %-8s %3lu.%lu%%  ", slot_names[i],
                   (unsigned long)(tenths / 10u), (unsigned long)(tenths % 10u));
            print_us(slot_max_us[i]);
            printf("\n");
        }
        printf("  passes:  %lu per second, slowest ",
               (unsigned long)((uint64_t)last_passes * 1000000u / (last_us ? last_us : 1u)));
        print_us(pass_max_us);
        printf("\n");
    }

    // Core 1.
    uint32_t fdebug = MIA_ACT_PIO->fdebug;
    bool stalled = (fdebug >> (PIO_FDEBUG_RXSTALL_LSB + MIA_ACT_SM)) & 1u;
    printf("Core 1 bus loop:\n");
    printf("  FIFO:     peak %u of 4 events waiting since clear; %s\n",
           (unsigned)act_fifo_peak,
           stalled ? "events LOST (RXSTALL, see status pio)" : "no events lost");
#if MIA_DIAG
    uint64_t window = (uint64_t)last_us * cycles_per_us;
    uint32_t tenths = window ? (uint32_t)((uint64_t)c1_busy_last * 1000u / window) : 0u;
    if (tenths > 1000u) {
        tenths = 1000u;
    }
    printf("  busy:     %lu.%lu%% handling bus events, %lu events per second\n",
           (unsigned long)(tenths / 10u), (unsigned long)(tenths % 10u),
           (unsigned long)((uint64_t)c1_events_last * 1000000u / (last_us ? last_us : 1u)));
#else
    printf("  busy:     only in the diagnostic build (make diag)\n");
#endif
}
