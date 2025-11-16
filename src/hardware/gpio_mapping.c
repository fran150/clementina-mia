/**
 * GPIO Pin Mapping Implementation
 * Hardware abstraction for 6502 bus interface
 */

#include "gpio_mapping.h"
#include "hardware/gpio.h"

// GPIO configuration table for batch initialization
typedef struct {
    uint8_t pin;
    uint8_t dir;
    uint8_t pull;
} gpio_config_t;

// Pull resistor constants
#define GPIO_PULL_NONE 0
#define GPIO_PULL_UP   1
#define GPIO_PULL_DOWN 2

static const gpio_config_t gpio_configs[] = {
    // Address bus pins (A0-A7) - inputs with pull-down
    {GPIO_ADDR_A0, GPIO_IN, GPIO_PULL_DOWN},
    {GPIO_ADDR_A1, GPIO_IN, GPIO_PULL_DOWN},
    {GPIO_ADDR_A2, GPIO_IN, GPIO_PULL_DOWN},
    {GPIO_ADDR_A3, GPIO_IN, GPIO_PULL_DOWN},
    {GPIO_ADDR_A4, GPIO_IN, GPIO_PULL_DOWN},
    {GPIO_ADDR_A5, GPIO_IN, GPIO_PULL_DOWN},
    {GPIO_ADDR_A6, GPIO_IN, GPIO_PULL_DOWN},
    {GPIO_ADDR_A7, GPIO_IN, GPIO_PULL_DOWN},
    
    // Data bus pins (D0-D7) - inputs initially with pull-down
    {GPIO_DATA_D0, GPIO_IN, GPIO_PULL_DOWN},
    {GPIO_DATA_D1, GPIO_IN, GPIO_PULL_DOWN},
    {GPIO_DATA_D2, GPIO_IN, GPIO_PULL_DOWN},
    {GPIO_DATA_D3, GPIO_IN, GPIO_PULL_DOWN},
    {GPIO_DATA_D4, GPIO_IN, GPIO_PULL_DOWN},
    {GPIO_DATA_D5, GPIO_IN, GPIO_PULL_DOWN},
    {GPIO_DATA_D6, GPIO_IN, GPIO_PULL_DOWN},
    {GPIO_DATA_D7, GPIO_IN, GPIO_PULL_DOWN},
    
    // Control signals - inputs with pull-up (active low)
    {GPIO_WE, GPIO_IN, GPIO_PULL_UP},
    {GPIO_OE, GPIO_IN, GPIO_PULL_UP},
    {GPIO_ROM_CS, GPIO_IN, GPIO_PULL_UP},
    {GPIO_VIDEO_CS, GPIO_IN, GPIO_PULL_UP},
    {GPIO_GEN_CS, GPIO_IN, GPIO_PULL_UP},
    
    // Control outputs
    {GPIO_PICOHIRAM, GPIO_OUT, GPIO_PULL_NONE},
    {GPIO_RESET_OUT, GPIO_OUT, GPIO_PULL_NONE},
    {GPIO_IRQ_OUT, GPIO_OUT, GPIO_PULL_NONE},
    {GPIO_CLK_OUT, GPIO_OUT, GPIO_PULL_NONE},
};

void gpio_mapping_init(void) {
    // Initialize all GPIOs from configuration table
    for (size_t i = 0; i < sizeof(gpio_configs) / sizeof(gpio_configs[0]); i++) {
        const gpio_config_t *cfg = &gpio_configs[i];
        gpio_init(cfg->pin);
        gpio_set_dir(cfg->pin, cfg->dir);
        
        // Set pull resistors
        if (cfg->pull == GPIO_PULL_UP) {
            gpio_pull_up(cfg->pin);
        } else if (cfg->pull == GPIO_PULL_DOWN) {
            gpio_pull_down(cfg->pin);
        }
    }
    
    // Set initial output states
    gpio_put(GPIO_PICOHIRAM, 1);    // Start with PICOHIRAM deasserted
    gpio_put(GPIO_RESET_OUT, 0);    // Start with reset asserted
    gpio_put(GPIO_IRQ_OUT, 1);      // Start with IRQ deasserted (active low)
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

