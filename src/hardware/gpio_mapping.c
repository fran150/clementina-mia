/**
 * GPIO Pin Mapping Implementation
 * Hardware abstraction for 6502 bus interface
 */

#include "debug/debug_helper.h"
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
    // Address bus pins (A0-A7) - inputs with no pull (6502 has push-pull outputs)
    {GPIO_ADDR_A0, GPIO_IN, GPIO_PULL_NONE},
    {GPIO_ADDR_A1, GPIO_IN, GPIO_PULL_NONE},
    {GPIO_ADDR_A2, GPIO_IN, GPIO_PULL_NONE},
    {GPIO_ADDR_A3, GPIO_IN, GPIO_PULL_NONE},
    {GPIO_ADDR_A4, GPIO_IN, GPIO_PULL_NONE},
    {GPIO_ADDR_A5, GPIO_IN, GPIO_PULL_NONE},
    {GPIO_ADDR_A6, GPIO_IN, GPIO_PULL_NONE},
    {GPIO_ADDR_A7, GPIO_IN, GPIO_PULL_NONE},
    
    // Data bus pins (D0-D7) - inputs initially with no pull (6502 has push-pull outputs)
    {GPIO_DATA_D0, GPIO_IN, GPIO_PULL_NONE},
    {GPIO_DATA_D1, GPIO_IN, GPIO_PULL_NONE},
    {GPIO_DATA_D2, GPIO_IN, GPIO_PULL_NONE},
    {GPIO_DATA_D3, GPIO_IN, GPIO_PULL_NONE},
    {GPIO_DATA_D4, GPIO_IN, GPIO_PULL_NONE},
    {GPIO_DATA_D5, GPIO_IN, GPIO_PULL_NONE},
    {GPIO_DATA_D6, GPIO_IN, GPIO_PULL_NONE},
    {GPIO_DATA_D7, GPIO_IN, GPIO_PULL_NONE},
    
    // Control signals - inputs with pull-up (active low)
    {GPIO_WE, GPIO_IN, GPIO_PULL_NONE},
    {GPIO_OE, GPIO_IN, GPIO_PULL_NONE},
    {GPIO_HIRAM_CS, GPIO_IN, GPIO_PULL_NONE},
    {GPIO_IO0_CS, GPIO_IN, GPIO_PULL_NONE},
    
    // Control outputs
    {GPIO_PICOHIRAM, GPIO_OUT, GPIO_PULL_NONE},
    {GPIO_RESET_OUT, GPIO_OUT, GPIO_PULL_NONE},
    {GPIO_IRQ_OUT, GPIO_OUT, GPIO_PULL_NONE},
    {GPIO_CLK_OUT, GPIO_OUT, GPIO_PULL_NONE},
};

void gpio_mapping_init(void) {
    log_print(LOG_INFO, "GPIO mapping initializing\n");

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
        } else {
            gpio_disable_pulls(cfg->pin);
        }
    }
    
    // Set initial output states
    gpio_put(GPIO_PICOHIRAM, 1);    // Start with PICOHIRAM deasserted
    gpio_put(GPIO_RESET_OUT, 0);    // Start with reset asserted
    gpio_put(GPIO_IRQ_OUT, 1);      // Start with IRQ deasserted (active low)
}

uint8_t gpio_read_address_bus(void) {
    return (uint8_t)((gpio_get_all() >> GPIO_ADDR_A0) & 0xFFu);
}

uint8_t gpio_read_data_bus(void) {
    return (uint8_t)((gpio_get_all() >> GPIO_DATA_D0) & 0xFFu);
}

void gpio_write_data_bus(uint8_t data) {
    uint32_t mask  = 0xFFu << GPIO_DATA_D0;
    uint32_t value = (uint32_t)data << GPIO_DATA_D0;

    gpio_put_masked(mask, value);
}

void gpio_set_data_bus_direction(bool output) {
  for (int i = GPIO_DATA_D0; i <= GPIO_DATA_D7; i++) {
    gpio_set_dir(i, output);
    // No pull resistors needed - 6502 has push-pull outputs
  }
}

void gpio_read_control_signals(bool *we, bool *oe, bool *hiram_cs, bool *io0_cs) {
  *we = !gpio_get(GPIO_WE);         // Active low
  *oe = !gpio_get(GPIO_OE);         // Active low
  *hiram_cs = !gpio_get(GPIO_HIRAM_CS); // Active low
  *io0_cs = !gpio_get(GPIO_IO0_CS); // Active low
}

