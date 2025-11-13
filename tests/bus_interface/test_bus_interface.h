/**
 * MIA Bus Interface Test Interface
 * 
 * Test functions for verifying bus interface functionality
 */

#ifndef TEST_BUS_INTERFACE_H
#define TEST_BUS_INTERFACE_H

#include <stdbool.h>

// Test function prototypes
bool test_bus_interface_address_decoding(void);
bool test_bus_interface_window_detection(void);
bool test_bus_interface_register_mirroring(void);
bool test_bus_interface_window_state_init(void);
bool test_bus_interface_window_index_access(void);
bool test_bus_interface_config_field_access(void);
bool test_bus_interface_window_independence(void);
bool test_bus_interface_direct_access(void);
bool test_bus_interface_idx_select_read(void);
bool test_bus_interface_data_port_step_sizes(void);
bool test_bus_interface_data_port_directions(void);
bool test_bus_interface_data_port_wrap_on_limit(void);
bool test_bus_interface_data_port_sequential_operations(void);
bool test_bus_interface_cfg_field_select_read(void);
bool test_bus_interface_cfg_field_select_write(void);
bool test_bus_interface_cfg_data_read(void);
bool test_bus_interface_cfg_data_write(void);
bool test_bus_interface_cfg_data_multibyte_fields(void);
bool test_bus_interface_cfg_data_dma_fields(void);
bool test_bus_interface_cfg_multi_window(void);

// Shared register tests
bool test_bus_interface_device_status_read(void);
bool test_bus_interface_irq_cause_low_read(void);
bool test_bus_interface_irq_cause_high_read(void);
bool test_bus_interface_irq_cause_write_to_clear(void);
bool test_bus_interface_irq_mask_read_write(void);
bool test_bus_interface_irq_enable_read_write(void);
bool test_bus_interface_irq_line_behavior(void);
bool test_bus_interface_individual_interrupt_bits(void);

// Main test runner
bool run_bus_interface_tests(void);

#endif // TEST_BUS_INTERFACE_H
