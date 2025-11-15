/**
 * Test suite for PIO-C FIFO communication protocol
 * 
 * Tests the FIFO communication between PIO and C code for the synchronous
 * bus interface, including timing verification and overflow/underflow handling.
 */

#ifndef TEST_BUS_SYNC_PIO_FIFO_H
#define TEST_BUS_SYNC_PIO_FIFO_H

/**
 * Run all FIFO communication protocol tests
 * 
 * This test suite verifies:
 * - FIFO initialization and configuration
 * - RX FIFO (PIO → C): Address byte at 60ns, Data byte for WRITE at 1000ns
 * - TX FIFO (C → PIO): Control byte, Data byte for READ
 * - PIO IRQ 0 handler triggering when CS is active
 * - FIFO timing: C must respond by ~560ns for PIO to drive data by 985ns
 * - FIFO overflow/underflow handling
 */
void test_bus_sync_pio_fifo_run_all(void);

/**
 * Get test results
 * 
 * @param run Pointer to store number of tests run
 * @param passed Pointer to store number of tests passed
 * @param failed Pointer to store number of tests failed
 */
void test_bus_sync_pio_fifo_get_results(int *run, int *passed, int *failed);

#endif // TEST_BUS_SYNC_PIO_FIFO_H
