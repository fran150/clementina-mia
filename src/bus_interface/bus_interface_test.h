/**
 * MIA Bus Interface Test Interface
 * 
 * Test functions for verifying bus interface functionality
 */

#ifndef BUS_INTERFACE_TEST_H
#define BUS_INTERFACE_TEST_H

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

// Main test runner
bool run_bus_interface_tests(void);

#endif // BUS_INTERFACE_TEST_H
