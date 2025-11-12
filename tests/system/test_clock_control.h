/**
 * Clock Control Test Functions Header
 */

#ifndef TEST_CLOCK_CONTROL_H
#define TEST_CLOCK_CONTROL_H

#include <stdbool.h>

// Test function prototypes
void clock_control_test_basic_functionality(void);
void clock_control_test_frequency_accuracy(void);

// Main test runner
bool run_clock_control_tests(void);

#endif // TEST_CLOCK_CONTROL_H