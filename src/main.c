/**
 * MIA (Multifunction Interface Adapter) Main Entry Point
 * Raspberry Pi Pico 2 W implementation for Clementina 6502 computer
 */

#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"
#include "debug/debug_helper.h"
#include "hardware/gpio_mapping.h"
#include "system/phi2_clock.h"
#include "system/reset_control.h"
#include "system/led_status.h"
#include "rom_emulation/rom_emulator.h"
#include "indexed_memory/indexed_memory.h"
#include "bus_interface/bus_interface.h"
#include "bus_interface/bus_sync_pio.h"
#include "video/video_controller.h"
#include "usb/usb_controller.h"
#include "network/wifi_controller.h"
#include "pico/bootrom.h"

// Core 1 entry point for video processing
void supporting_functions_loop() {    
    while (true) {
        video_controller_process();
        usb_controller_process();
        wifi_controller_process();
        indexed_memory_process_copy_command();
        
        eval_keyboard();

        tight_loop_contents();
    }
}

int main() {
    // Initialize LED status system first
    led_status_init();
    
    // Initialize standard I/O
    stdio_init_all();

    // Initialize video controller (Core 0 portion)
    video_controller_init();
    
    // Initialize USB controller (mode detection and setup)
    usb_controller_init();

    // wifi_controller_init();
    // log_print(LOG_INFO, "[Wi-Fi] Controller Initialized.\n");

    
    // Wait for USB enumeration
    sleep_ms(2000);
    led_status_set(LED_STATUS_USB_READY);
    
    log_print(LOG_INFO, "MIA (Multifunction Interface Adapter) Starting...\n");
    
    // Initialize GPIO pin mappings
    gpio_mapping_init();
    
    
    // Initialize clock control system
    phi2_clock_init(100000);  // Example: 1 MHz PHI2 clock
    log_print(LOG_INFO, "Clock control initialized\n");
    
    // Initialize IRQ system first
    irq_init();
    log_print(LOG_INFO, "IRQ system initialized\n");
    
    // Initialize indexed memory system
    indexed_memory_init();
    log_print(LOG_INFO, "Indexed memory system initialized\n");

    // Initialize ROM emulator for boot phase
    rom_emulator_init();
    log_print(LOG_INFO, "ROM emulator initialized\n");
                
    // Start the boot sequence
    rom_emulator_start_boot_sequence();
    log_print(LOG_INFO, "Starting boot sequence...\n");
    
    // Core 0 main loop - system control
    while (rom_emulator_is_active()) {
        // Handle ROM emulation during boot phase
        rom_emulator_process();
                
        // Handle reset control
        reset_control_process();
        
        // Update LED status
        led_status_update();

        eval_keyboard();
    }

    log_print(LOG_INFO, "Boot sequence completed. Transitioning to normal operation...\n");
    led_status_set(LED_STATUS_BOOT_COMPLETE);

    // Launch Core 1 for video processing
    multicore_launch_core1(supporting_functions_loop);
    log_print(LOG_INFO, "Enabling Core 1 for Video, USB and Wi-Fi support\n");

    // Activate bus interface for normal MIA operations
    log_print(LOG_INFO, "Initializing bus interface...\n");
    bus_interface_init();
    bus_sync_pio_init();
    log_print(LOG_INFO, "Bus interface initialized\n");

    // Set running status
    led_status_set(LED_STATUS_RUNNING);

    while (true) {
        led_status_update();
        tight_loop_contents();
    }
    
    return 0;
}