/**
 * GPIO Pin Mapping Constants for MIA Hardware Interface
 * Defines all GPIO assignments for 6502 bus interface and control signals
 */

#ifndef GPIO_MAPPING_H
#define GPIO_MAPPING_H

#include <stdint.h>
#include <stdbool.h>

// Address Bus Lines (8-bit addressing for 256-byte ROM space with mirroring)
#define GPIO_ADDR_A0    0
#define GPIO_ADDR_A1    1
#define GPIO_ADDR_A2    2
#define GPIO_ADDR_A3    3
#define GPIO_ADDR_A4    4
#define GPIO_ADDR_A5    5
#define GPIO_ADDR_A6    6
#define GPIO_ADDR_A7    7

// Data Bus Lines (bidirectional)
#define GPIO_DATA_D0    8
#define GPIO_DATA_D1    9
#define GPIO_DATA_D2    10
#define GPIO_DATA_D3    11
#define GPIO_DATA_D4    12
#define GPIO_DATA_D5    13
#define GPIO_DATA_D6    14
#define GPIO_DATA_D7    15

// Control Signals
#define GPIO_PICOHIRAM  16  // Active low - banks MIA into high memory
#define GPIO_RESET_OUT  17  // Reset line output to Clementina system (active low)
#define GPIO_WE         18  // Write Enable input from 6502 (active low)
#define GPIO_OE         19  // Output Enable input from 6502 (active low)

// Chip Select Lines
#define GPIO_ROM_CS     20  // ROM Emulation Chip Select input (active low)
#define GPIO_VIDEO_CS   21  // Video Chip Select input (Device 4) (active low)
#define GPIO_GEN_CS     22  // General Interface Chip Select input (Device 0) (active low)

// Interrupt Line
#define GPIO_IRQ_OUT    26  // IRQ line output to 6502 CPU (active low)

// System Control
#define GPIO_CLK_OUT    28  // Clock output (PWM6A) to Clementina

// Address bus mask for 8-bit addressing
#define ADDR_BUS_MASK   0xFF

// Data bus mask for 8-bit data
#define DATA_BUS_MASK   0xFF

// Function prototypes
void gpio_mapping_init(void);
uint16_t gpio_read_address_bus(void);
uint8_t gpio_read_data_bus(void);
void gpio_write_data_bus(uint8_t data);
void gpio_set_data_bus_direction(bool output);
bool gpio_read_control_signals(bool *we, bool *oe, bool *rom_cs, bool *video_cs, bool *gen_cs);


#endif // GPIO_MAPPING_H