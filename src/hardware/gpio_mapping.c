/**
 * GPIO Pin Mapping Implementation
 * Hardware abstraction for 6502 bus interface
 */

#include "gpio_mapping.h"
#include "hardware/gpio.h"

void gpio_mapping_init(void) {
  // Initialize address bus pins as inputs (A0-A7)
  for (int i = GPIO_ADDR_A0; i <= GPIO_ADDR_A7; i++) {
    gpio_init(i);
    gpio_set_dir(i, GPIO_IN);
    gpio_pull_down(i); // Pull down for stable readings
  }
  
  
  // Initialize data bus pins as inputs initially (will be switched to output
  // when needed)
  for (int i = GPIO_DATA_D0; i <= GPIO_DATA_D7; i++) {
    gpio_init(i);
    gpio_set_dir(i, GPIO_IN);
    gpio_pull_down(i);
  }

  // Initialize control signal inputs
  gpio_init(GPIO_WE);
  gpio_set_dir(GPIO_WE, GPIO_IN);
  gpio_pull_up(GPIO_WE); // WE is active low

  gpio_init(GPIO_OE);
  gpio_set_dir(GPIO_OE, GPIO_IN);
  gpio_pull_up(GPIO_OE); // OE is active low

  // Initialize chip select inputs
  gpio_init(GPIO_ROM_CS);
  gpio_set_dir(GPIO_ROM_CS, GPIO_IN);
  gpio_pull_up(GPIO_ROM_CS); // CS is active low

  gpio_init(GPIO_VIDEO_CS);
  gpio_set_dir(GPIO_VIDEO_CS, GPIO_IN);
  gpio_pull_up(GPIO_VIDEO_CS); // CS is active low

  gpio_init(GPIO_GEN_CS);
  gpio_set_dir(GPIO_GEN_CS, GPIO_IN);
  gpio_pull_up(GPIO_GEN_CS); // CS is active low

  // Initialize control outputs
  gpio_init(GPIO_PICOHIRAM);
  gpio_set_dir(GPIO_PICOHIRAM, GPIO_OUT);
  gpio_put(GPIO_PICOHIRAM, 1); // Start with MIA banked out (inactive)

  gpio_init(GPIO_RESET_OUT);
  gpio_set_dir(GPIO_RESET_OUT, GPIO_OUT);
  gpio_put(GPIO_RESET_OUT, 1); // Start with reset deasserted (active low)

  // Clock output will be initialized by clock_control module
}

uint16_t gpio_read_address_bus(void) {
  uint16_t address = 0;

  // Read A0-A7 (GPIO 0-7) - 8-bit addressing
  for (int i = 0; i < 8; i++) {
    if (gpio_get(GPIO_ADDR_A0 + i)) {
      address |= (1 << i);
    }
  }

  return address & ADDR_BUS_MASK;
}

uint8_t gpio_read_data_bus(void) {
  uint8_t data = 0;

  for (int i = 0; i < 8; i++) {
    if (gpio_get(GPIO_DATA_D0 + i)) {
      data |= (1 << i);
    }
  }

  return data;
}

void gpio_write_data_bus(uint8_t data) {
  for (int i = 0; i < 8; i++) {
    gpio_put(GPIO_DATA_D0 + i, (data >> i) & 1);
  }
}

void gpio_set_data_bus_direction(bool output) {
  bool direction = output;

  for (int i = GPIO_DATA_D0; i <= GPIO_DATA_D7; i++) {
    gpio_set_dir(i, direction ? GPIO_OUT : GPIO_IN);
    if (!output) {
      gpio_pull_down(i); // Pull down when input
    }
  }
}

bool gpio_read_control_signals(bool *we, bool *oe, bool *rom_cs, bool *video_cs,
                               bool *gen_cs) {
  if (we)
    *we = !gpio_get(GPIO_WE); // Active low
  if (oe)
    *oe = !gpio_get(GPIO_OE); // Active low
  if (rom_cs)
    *rom_cs = !gpio_get(GPIO_ROM_CS); // Active low
  if (video_cs)
    *video_cs = !gpio_get(GPIO_VIDEO_CS); // Active low
  if (gen_cs)
    *gen_cs = !gpio_get(GPIO_GEN_CS); // Active low

  return true;
}

