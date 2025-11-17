/**
 * MIA (Multifunction Interface Adapter) Main Entry Point
 * Raspberry Pi Pico 2 W implementation for Clementina 6502 computer
 */

#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"
#include "hardware/clocks.h"

#include "hardware/gpio_mapping.h"
#include "system/clock_control.h"
#include "system/reset_control.h"
#include "rom_emulation/rom_emulator.h"
#include "indexed_memory/indexed_memory.h"
#include "bus_interface/bus_interface.h"
#include "bus_interface/bus_sync_pio.h"
#include "video/video_controller.h"
#include "usb/usb_controller.h"
#include "network/wifi_controller.h"

// Core 1 entry point for video processing
void supporting_functions_loop() {
    // Initialize video controller (Core 0 portion)
    video_controller_init();
    printf("[Video] Controller Initialized.\n");
    
    // // Initialize USB controller (mode detection and setup)
    usb_controller_init();
    printf("[USB] Controller Initialized.\n");

    wifi_controller_init();
    printf("[Wi-Fi] Controller Initialized.\n");

    while (true) {
        video_controller_process();
        usb_controller_process();
        wifi_controller_process();
        indexed_memory_process_copy_command();
        
        tight_loop_contents();
    }
}

int main() {
    // Initialize standard I/O
    stdio_init_all();
    
    printf("MIA (Multifunction Interface Adapter) Starting...\n");
    
    // Initialize GPIO pin mappings
    gpio_mapping_init();
    printf("GPIO mapping initialized\n");
    
    // Initialize clock control system
    clock_control_init();
    printf("Clock control initialized\n");
    
    // Initialize IRQ system first
    irq_init();
    printf("IRQ system initialized\n");

    // Initialize indexed memory system
    indexed_memory_init();
    printf("Indexed memory system initialized\n");

    // Initialize ROM emulator for boot phase
    rom_emulator_init();
    printf("ROM emulator initialized\n");
                
    // Start the boot sequence
    rom_emulator_start_boot_sequence();
    printf("Starting boot sequence...\n");
    
    // Core 0 main loop - system control
    while (rom_emulator_is_active()) {
        // Handle ROM emulation during boot phase
        rom_emulator_process();
                
        // Handle reset control
        reset_control_process();
    }

    printf("Boot sequence completed. Transitioning to normal operation...\n");

    // Launch Core 1 for video processing
    multicore_launch_core1(supporting_functions_loop);
    printf("Enabling Core 1 for Video, USB and Wi-Fi support\n");

    // Activate bus interface for normal MIA operations
    printf("Initializing bus interface...\n");
    bus_interface_init();
    bus_sync_pio_init();
    printf("Bus interface initialized\n");

    while (true) {
        tight_loop_contents();
    }
    
    return 0;
}