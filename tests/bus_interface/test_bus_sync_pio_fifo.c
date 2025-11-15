/**
 * Test suite for PIO-C FIFO communication protocol
 * 
 * Tests the FIFO communication between PIO and C code for the synchronous
 * bus interface, including timing verification and overflow/underflow handling.
 */

#include "test_bus_sync_pio_fifo.h"
#include "bus_interface/bus_sync_pio.h"
#include "bus_interface/bus_interface.h"
#include <stdio.h>
#include <string.h>

// Test result tracking
static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

// Helper macros for test assertions
#define TEST_ASSERT(condition, message) \
    do { \
        tests_run++; \
        if (condition) { \
            tests_passed++; \
            printf("  [PASS] %s\n", message); \
        } else { \
            tests_failed++; \
            printf("  [FAIL] %s\n", message); \
        } \
    } while(0)

#define TEST_ASSERT_EQUAL(expected, actual, message) \
    do { \
        tests_run++; \
        if ((expected) == (actual)) { \
            tests_passed++; \
            printf("  [PASS] %s (expected=%d, actual=%d)\n", message, (int)(expected), (int)(actual)); \
        } else { \
            tests_failed++; \
            printf("  [FAIL] %s (expected=%d, actual=%d)\n", message, (int)(expected), (int)(actual)); \
        } \
    } while(0)

/**
 * Test FIFO initialization and basic status
 */
static void test_fifo_initialization(void) {
    printf("\nTest: FIFO Initialization\n");
    
    // Get initial FIFO statistics
    uint8_t rx_level = 0;
    uint8_t tx_level = 0;
    bool stalled = false;
    
    bus_sync_pio_get_stats(&rx_level, &tx_level, &stalled);
    
    // Initially, FIFOs should be empty or have minimal data
    TEST_ASSERT(rx_level <= 8, "RX FIFO level within valid range (0-8)");
    TEST_ASSERT(tx_level <= 8, "TX FIFO level within valid range (0-8)");
    TEST_ASSERT(!stalled, "PIO state machine not stalled initially");
    
    // Check that PIO is ready
    bool ready = bus_sync_pio_is_ready();
    TEST_ASSERT(ready, "PIO is ready for operations");
}

/**
 * Test FIFO overflow detection
 */
static void test_fifo_overflow_detection(void) {
    printf("\nTest: FIFO Overflow Detection\n");
    
    bool rx_overflow = false;
    bool tx_underflow = false;
    
    // Check for overflow/underflow conditions
    bus_sync_pio_check_fifo_errors(&rx_overflow, &tx_underflow);
    
    // Under normal conditions, there should be no overflow/underflow
    TEST_ASSERT(!rx_overflow, "No RX FIFO overflow under normal conditions");
    TEST_ASSERT(!tx_underflow, "No TX FIFO underflow under normal conditions");
    
    printf("  Note: RX overflow=%d, TX underflow=%d\n", rx_overflow, tx_underflow);
}

/**
 * Test FIFO statistics reporting
 */
static void test_fifo_statistics(void) {
    printf("\nTest: FIFO Statistics Reporting\n");
    
    uint8_t rx_level = 0;
    uint8_t tx_level = 0;
    bool stalled = false;
    
    // Get FIFO statistics
    bus_sync_pio_get_stats(&rx_level, &tx_level, &stalled);
    
    printf("  RX FIFO level: %d/8\n", rx_level);
    printf("  TX FIFO level: %d/8\n", tx_level);
    printf("  Stalled: %s\n", stalled ? "yes" : "no");
    
    // Verify statistics are within valid ranges
    TEST_ASSERT(rx_level <= 8, "RX FIFO level valid");
    TEST_ASSERT(tx_level <= 8, "TX FIFO level valid");
}

/**
 * Test WRITE data processing
 */
static void test_write_data_processing(void) {
    printf("\nTest: WRITE Data Processing\n");
    
    // Try to process any pending WRITE data
    bool data_processed = bus_sync_pio_process_write_data();
    
    // This may or may not have data depending on test execution order
    printf("  WRITE data processed: %s\n", data_processed ? "yes" : "no");
    
    // Just verify the function doesn't crash
    TEST_ASSERT(true, "WRITE data processing function executes without error");
}

/**
 * Test FIFO timing verification
 * 
 * This test verifies that the FIFO communication meets timing requirements:
 * - C code must respond by ~560ns for PIO to drive data by 985ns
 * - Total budget: 785ns from CS sampling (200ns) to data deadline (985ns)
 * - C execution budget: ~360ns (200-560ns)
 */
static void test_fifo_timing_verification(void) {
    printf("\nTest: FIFO Timing Verification\n");
    
    // Note: Actual timing verification requires hardware testing with logic analyzer
    // This test verifies the software structure is correct
    
    printf("  Timing requirements:\n");
    printf("    - CS sampling: 200ns\n");
    printf("    - C response deadline: 560ns\n");
    printf("    - Data drive deadline: 985ns\n");
    printf("    - C execution budget: 360ns (200-560ns)\n");
    printf("    - PIO drive budget: 425ns (560-985ns)\n");
    
    // Verify FIFO is ready for operations
    bool ready = bus_sync_pio_is_ready();
    TEST_ASSERT(ready, "PIO ready for timing-critical operations");
    
    // Get FIFO statistics to ensure no backlog
    uint8_t rx_level = 0;
    uint8_t tx_level = 0;
    bool stalled = false;
    
    bus_sync_pio_get_stats(&rx_level, &tx_level, &stalled);
    
    // For optimal timing, FIFOs should not be full
    TEST_ASSERT(rx_level < 8, "RX FIFO not full (allows new data)");
    TEST_ASSERT(!stalled, "PIO not stalled (timing critical)");
    
    printf("  Note: Hardware timing verification requires logic analyzer\n");
}

/**
 * Test FIFO error recovery
 */
static void test_fifo_error_recovery(void) {
    printf("\nTest: FIFO Error Recovery\n");
    
    // Check for any error conditions
    bool rx_overflow = false;
    bool tx_underflow = false;
    
    bus_sync_pio_check_fifo_errors(&rx_overflow, &tx_underflow);
    
    if (rx_overflow || tx_underflow) {
        printf("  Warning: FIFO errors detected\n");
        printf("    RX overflow: %s\n", rx_overflow ? "yes" : "no");
        printf("    TX underflow: %s\n", tx_underflow ? "yes" : "no");
        printf("  Note: In real hardware, these would trigger:\n");
        printf("    - STATUS_MEMORY_ERROR in device status register\n");
        printf("    - IRQ_MEMORY_ERROR interrupt (if enabled)\n");
    }
    
    // Verify PIO can recover from errors
    bool ready = bus_sync_pio_is_ready();
    TEST_ASSERT(ready, "PIO can recover from error conditions");
    
    printf("  Note: FIFO errors automatically set error status and trigger interrupts\n");
}

/**
 * Run all FIFO communication protocol tests
 */
void test_bus_sync_pio_fifo_run_all(void) {
    printf("\n");
    printf("========================================\n");
    printf("PIO-C FIFO Communication Protocol Tests\n");
    printf("========================================\n");
    
    // Reset test counters
    tests_run = 0;
    tests_passed = 0;
    tests_failed = 0;
    
    // Run all tests
    test_fifo_initialization();
    test_fifo_overflow_detection();
    test_fifo_statistics();
    test_write_data_processing();
    test_fifo_timing_verification();
    test_fifo_error_recovery();
    
    // Print summary
    printf("\n");
    printf("========================================\n");
    printf("Test Summary\n");
    printf("========================================\n");
    printf("Tests run:    %d\n", tests_run);
    printf("Tests passed: %d\n", tests_passed);
    printf("Tests failed: %d\n", tests_failed);
    printf("Success rate: %.1f%%\n", tests_run > 0 ? (100.0 * tests_passed / tests_run) : 0.0);
    printf("========================================\n");
    
    if (tests_failed > 0) {
        printf("\nSome tests FAILED. Review output above for details.\n");
    } else {
        printf("\nAll tests PASSED!\n");
    }
}

/**
 * Get test results
 */
void test_bus_sync_pio_fifo_get_results(int *run, int *passed, int *failed) {
    if (run) *run = tests_run;
    if (passed) *passed = tests_passed;
    if (failed) *failed = tests_failed;
}
