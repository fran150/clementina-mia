/**
 * MIA Indexed Memory System Test Interface
 * 
 * Test functions for verifying indexed memory system functionality
 */

#ifndef TEST_INDEXED_MEMORY_H
#define TEST_INDEXED_MEMORY_H

#include <stdbool.h>

// Test function prototypes
bool test_indexed_memory_init(void);
bool test_index_structure(void);
bool test_basic_memory_access(void);
bool test_auto_stepping(void);
bool test_configuration_fields(void);
bool test_dma_operations(void);
bool test_window_management(void);
bool test_error_handling(void);

// Main test runner
bool run_indexed_memory_tests(void);

#endif // TEST_INDEXED_MEMORY_H