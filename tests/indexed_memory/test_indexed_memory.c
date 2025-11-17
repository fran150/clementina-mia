/**
 * MIA Indexed Memory System Tests
 * 
 * Comprehensive test suite for the indexed memory system
 */

#include "test_indexed_memory.h"
#include "indexed_memory/indexed_memory.h"
#include "irq/irq.h"
#include "mocks/pico_mock.h"
#include "bus_interface/bus_interface.h"
#include <stdio.h>
#include <string.h>

// Test setup helper - initializes dependencies in correct order
static void test_setup_indexed_memory(void) {
    irq_init();                    // Initialize IRQ system first
    indexed_memory_init();         // Then initialize indexed memory
}

// Helper functions to replace internal function calls with public API
static void test_set_index_address(uint8_t idx, uint32_t address) {
    indexed_memory_set_config_field(idx, CFG_ADDR_L, address & 0xFF);
    indexed_memory_set_config_field(idx, CFG_ADDR_M, (address >> 8) & 0xFF);
    indexed_memory_set_config_field(idx, CFG_ADDR_H, (address >> 16) & 0xFF);
}

static void test_set_index_default(uint8_t idx, uint32_t address) {
    indexed_memory_set_config_field(idx, CFG_DEFAULT_L, address & 0xFF);
    indexed_memory_set_config_field(idx, CFG_DEFAULT_M, (address >> 8) & 0xFF);
    indexed_memory_set_config_field(idx, CFG_DEFAULT_H, (address >> 16) & 0xFF);
}

static void test_set_index_limit(uint8_t idx, uint32_t address) {
    indexed_memory_set_config_field(idx, CFG_LIMIT_L, address & 0xFF);
    indexed_memory_set_config_field(idx, CFG_LIMIT_M, (address >> 8) & 0xFF);
    indexed_memory_set_config_field(idx, CFG_LIMIT_H, (address >> 16) & 0xFF);
}

static void test_set_index_step(uint8_t idx, uint8_t step) {
    indexed_memory_set_config_field(idx, CFG_STEP, step);
}

static void test_set_index_flags(uint8_t idx, uint8_t flags) {
    indexed_memory_set_config_field(idx, CFG_FLAGS, flags);
}

static uint16_t test_get_irq_cause(void) {
    uint8_t low = irq_get_cause_low();
    uint8_t high = irq_get_cause_high();
    return (uint16_t)low | ((uint16_t)high << 8);
}

static void test_copy_block(uint8_t src_idx, uint8_t dst_idx, uint16_t count) {
    // Set up DMA configuration using config fields
    indexed_memory_set_config_field(0, CFG_COPY_SRC_IDX, src_idx);
    indexed_memory_set_config_field(0, CFG_COPY_DST_IDX, dst_idx);
    indexed_memory_set_config_field(0, CFG_COPY_COUNT_L, count & 0xFF);
    indexed_memory_set_config_field(0, CFG_COPY_COUNT_H, (count >> 8) & 0xFF);
    
    // Execute copy command
    indexed_memory_execute_shared_command(CMD_COPY_BLOCK);
    
    // Process queued commands (simulates Core 1 processing)
    // Call multiple times to ensure all queued commands are processed
    for (int i = 0; i < 10; i++) {
        indexed_memory_process_copy_command();
    }
}

// Helper to get current index address using public API
static inline uint32_t get_index_address(uint8_t idx) {
    uint8_t addr_l = indexed_memory_get_config_field(idx, CFG_ADDR_L);
    uint8_t addr_m = indexed_memory_get_config_field(idx, CFG_ADDR_M);
    uint8_t addr_h = indexed_memory_get_config_field(idx, CFG_ADDR_H);
    return addr_l | (addr_m << 8) | (addr_h << 16);
}

/**
 * Test indexed memory initialization
 */
bool test_indexed_memory_init(void) {
    printf("Testing indexed memory initialization...\n");
    
    test_setup_indexed_memory();
    
    // Check system status
    uint8_t status = bus_interface_read(0xF0);  // REG_DEVICE_STATUS
    if (!(status & STATUS_SYSTEM_READY)) {
        printf("FAIL: System not ready after init\n");
        return false;
    }
    
    // Check IRQ state
    uint8_t irq_cause = test_get_irq_cause();
    if (irq_cause != IRQ_NO_IRQ) {
        printf("FAIL: IRQ pending after init\n");
        return false;
    }
    
    // Check pre-configured indexes
    // Index 0 should be configured for system error log
    uint8_t step = indexed_memory_get_config_field(IDX_SYSTEM_ERROR, CFG_STEP);
    if (step != 1) {
        printf("FAIL: Index 0 step not configured correctly\n");
        return false;
    }
    
    uint8_t flags = indexed_memory_get_config_field(IDX_SYSTEM_ERROR, CFG_FLAGS);
    if (!(flags & FLAG_AUTO_STEP)) {
        printf("FAIL: Index 0 auto-step not enabled\n");
        return false;
    }
    
    printf("PASS: Indexed memory initialization\n");
    return true;
}

/**
 * Test index structure and basic operations
 */
bool test_index_structure(void) {
    printf("Testing index structure...\n");
    
    uint8_t test_idx = IDX_USER_START; // Use first user index
    
    // Test address setting
    uint32_t test_addr = 0x20013800; // MIA_USER_AREA_BASE
    test_set_index_address(test_idx, test_addr);
    
    uint8_t addr_l = indexed_memory_get_config_field(test_idx, CFG_ADDR_L);
    uint8_t addr_m = indexed_memory_get_config_field(test_idx, CFG_ADDR_M);
    uint8_t addr_h = indexed_memory_get_config_field(test_idx, CFG_ADDR_H);
    
    uint32_t read_addr = addr_l | (addr_m << 8) | (addr_h << 16);
    if (read_addr != (test_addr & 0xFFFFFF)) {
        printf("FAIL: Address setting/getting mismatch\n");
        return false;
    }
    
    // Test default address setting
    uint32_t default_addr = 0x20014800; // MIA_USER_AREA_BASE + 0x1000
    test_set_index_default(test_idx, default_addr);
    
    uint8_t def_l = indexed_memory_get_config_field(test_idx, CFG_DEFAULT_L);
    uint8_t def_m = indexed_memory_get_config_field(test_idx, CFG_DEFAULT_M);
    uint8_t def_h = indexed_memory_get_config_field(test_idx, CFG_DEFAULT_H);
    
    uint32_t read_default = def_l | (def_m << 8) | (def_h << 16);
    if (read_default != (default_addr & 0xFFFFFF)) {
        printf("FAIL: Default address setting/getting mismatch\n");
        return false;
    }
    
    // Test step size setting
    uint8_t test_step = 4;
    test_set_index_step(test_idx, test_step);
    
    uint8_t read_step = indexed_memory_get_config_field(test_idx, CFG_STEP);
    if (read_step != test_step) {
        printf("FAIL: Step size setting/getting mismatch\n");
        return false;
    }
    
    // Test flags setting
    uint8_t test_flags = FLAG_AUTO_STEP | FLAG_DIRECTION;
    test_set_index_flags(test_idx, test_flags);
    
    uint8_t read_flags = indexed_memory_get_config_field(test_idx, CFG_FLAGS);
    if (read_flags != test_flags) {
        printf("FAIL: Flags setting/getting mismatch\n");
        return false;
    }
    
    // Test index reset
    indexed_memory_execute_window_command(test_idx, CMD_RESET_INDEX);
    
    addr_l = indexed_memory_get_config_field(test_idx, CFG_ADDR_L);
    addr_m = indexed_memory_get_config_field(test_idx, CFG_ADDR_M);
    addr_h = indexed_memory_get_config_field(test_idx, CFG_ADDR_H);
    
    read_addr = addr_l | (addr_m << 8) | (addr_h << 16);
    if (read_addr != (default_addr & 0xFFFFFF)) {
        printf("FAIL: Index reset did not restore default address\n");
        return false;
    }
    
    printf("PASS: Index structure\n");
    return true;
}

/**
 * Test basic memory access
 */
bool test_basic_memory_access(void) {
    printf("Testing basic memory access...\n");
    
    uint8_t test_idx = IDX_USER_START;
    
    // Configure index for user memory area
    uint32_t test_addr = 0x20013800; // MIA_USER_AREA_BASE
    test_set_index_address(test_idx, test_addr);
    test_set_index_step(test_idx, 1);
    test_set_index_flags(test_idx, 0); // No auto-step for this test
    
    // Test write and read without stepping
    uint8_t test_data = 0xAB;
    indexed_memory_write(test_idx, test_data);
    uint8_t read_data = indexed_memory_read(test_idx);
    
    if (read_data != test_data) {
        printf("FAIL: Basic write/read mismatch (expected 0x%02X, got 0x%02X)\n", test_data, read_data);
        return false;
    }
    
    // Verify address didn't change
    uint8_t addr_l = indexed_memory_get_config_field(test_idx, CFG_ADDR_L);
    uint8_t addr_m = indexed_memory_get_config_field(test_idx, CFG_ADDR_M);
    uint8_t addr_h = indexed_memory_get_config_field(test_idx, CFG_ADDR_H);
    
    uint32_t current_addr = addr_l | (addr_m << 8) | (addr_h << 16);
    if (current_addr != (test_addr & 0xFFFFFF)) {
        printf("FAIL: Address changed during no-step access\n");
        return false;
    }
    
    printf("PASS: Basic memory access\n");
    return true;
}

/**
 * Test auto-stepping functionality
 */
bool test_auto_stepping(void) {
    printf("Testing auto-stepping functionality...\n");
    
    uint8_t test_idx = IDX_USER_START + 1;
    
    // Configure index with auto-stepping
    uint32_t start_addr = 0x20013900; // MIA_USER_AREA_BASE + 0x100
    test_set_index_default(test_idx, start_addr);
    test_set_index_address(test_idx, start_addr);
    test_set_index_step(test_idx, 2); // Step by 2 bytes
    test_set_index_flags(test_idx, FLAG_AUTO_STEP); // Forward stepping
    
    // Write test pattern with auto-stepping
    uint8_t test_pattern[] = {0x11, 0x22, 0x33, 0x44};
    for (int i = 0; i < 4; i++) {
        indexed_memory_write(test_idx, test_pattern[i]);
    }
    
    // Check final address (should have advanced by 4 * 2 = 8 bytes)
    uint8_t addr_l = indexed_memory_get_config_field(test_idx, CFG_ADDR_L);
    uint8_t addr_m = indexed_memory_get_config_field(test_idx, CFG_ADDR_M);
    uint8_t addr_h = indexed_memory_get_config_field(test_idx, CFG_ADDR_H);
    
    uint32_t final_addr = addr_l | (addr_m << 8) | (addr_h << 16);
    uint32_t expected_addr = start_addr + 8;
    
    if (final_addr != (expected_addr & 0xFFFFFF)) {
        printf("FAIL: Auto-stepping address incorrect (expected 0x%06lX, got 0x%06lX)\n", 
               (unsigned long)(expected_addr & 0xFFFFFF), (unsigned long)final_addr);
        return false;
    }
    
    // Reset and read back with auto-stepping
    indexed_memory_execute_window_command(test_idx, CMD_RESET_INDEX);
    
    for (int i = 0; i < 4; i++) {
        uint8_t read_data = indexed_memory_read(test_idx);
        if (read_data != test_pattern[i]) {
            printf("FAIL: Auto-step read mismatch at position %d (expected 0x%02X, got 0x%02X)\n", 
                   i, test_pattern[i], read_data);
            return false;
        }
    }
    
    // Test backward stepping
    test_set_index_flags(test_idx, FLAG_AUTO_STEP | FLAG_DIRECTION);
    test_set_index_address(test_idx, start_addr + 6); // Start at position of last write
    
    // Read backwards
    for (int i = 3; i >= 0; i--) {
        uint8_t read_data = indexed_memory_read(test_idx);
        if (read_data != test_pattern[i]) {
            printf("FAIL: Backward auto-step read mismatch at position %d (expected 0x%02X, got 0x%02X)\n", 
                   i, test_pattern[i], read_data);
            return false;
        }
    }
    
    printf("PASS: Auto-stepping functionality\n");
    return true;
}

/**
 * Test configuration field access
 */
bool test_configuration_fields(void) {
    printf("Testing configuration field access...\n");
    
    uint8_t test_idx = IDX_USER_START + 2;
    
    // Test all configuration fields
    indexed_memory_set_config_field(test_idx, CFG_ADDR_L, 0x12);
    indexed_memory_set_config_field(test_idx, CFG_ADDR_M, 0x34);
    indexed_memory_set_config_field(test_idx, CFG_ADDR_H, 0x56);
    
    if (indexed_memory_get_config_field(test_idx, CFG_ADDR_L) != 0x12 ||
        indexed_memory_get_config_field(test_idx, CFG_ADDR_M) != 0x34 ||
        indexed_memory_get_config_field(test_idx, CFG_ADDR_H) != 0x56) {
        printf("FAIL: Address field configuration\n");
        return false;
    }
    
    indexed_memory_set_config_field(test_idx, CFG_DEFAULT_L, 0x78);
    indexed_memory_set_config_field(test_idx, CFG_DEFAULT_M, 0x9A);
    indexed_memory_set_config_field(test_idx, CFG_DEFAULT_H, 0xBC);
    
    if (indexed_memory_get_config_field(test_idx, CFG_DEFAULT_L) != 0x78 ||
        indexed_memory_get_config_field(test_idx, CFG_DEFAULT_M) != 0x9A ||
        indexed_memory_get_config_field(test_idx, CFG_DEFAULT_H) != 0xBC) {
        printf("FAIL: Default address field configuration\n");
        return false;
    }
    
    indexed_memory_set_config_field(test_idx, CFG_STEP, 0xDE);
    if (indexed_memory_get_config_field(test_idx, CFG_STEP) != 0xDE) {
        printf("FAIL: Step field configuration\n");
        return false;
    }
    
    indexed_memory_set_config_field(test_idx, CFG_FLAGS, 0x03);
    if (indexed_memory_get_config_field(test_idx, CFG_FLAGS) != 0x03) {
        printf("FAIL: Flags field configuration\n");
        return false;
    }
    
    printf("PASS: Configuration field access\n");
    return true;
}

/**
 * Test DMA operations
 */
bool test_dma_operations(void) {
    printf("Testing DMA operations...\n");
    
    uint8_t src_idx = IDX_USER_START + 3;
    uint8_t dst_idx = IDX_USER_START + 4;
    
    // Set up source index with test data
    uint32_t src_addr = 0x20013A00; // MIA_USER_AREA_BASE + 0x200
    test_set_index_default(src_idx, src_addr);
    test_set_index_address(src_idx, src_addr);
    test_set_index_step(src_idx, 1);
    test_set_index_flags(src_idx, FLAG_AUTO_STEP);
    
    // Set up destination index
    uint32_t dst_addr = 0x20013B00; // MIA_USER_AREA_BASE + 0x300
    test_set_index_default(dst_idx, dst_addr);
    test_set_index_address(dst_idx, dst_addr);
    test_set_index_step(dst_idx, 1);
    test_set_index_flags(dst_idx, FLAG_AUTO_STEP);
    
    // Write test pattern to source
    uint8_t test_data[] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE};
    for (int i = 0; i < 5; i++) {
        indexed_memory_write(src_idx, test_data[i]);
    }
    
    // Test single byte copy using copy_block with count=1
    indexed_memory_execute_window_command(src_idx, CMD_RESET_INDEX);
    indexed_memory_execute_window_command(dst_idx, CMD_RESET_INDEX);
    
    uint32_t src_addr_before = get_index_address(src_idx);
    uint32_t dst_addr_before = get_index_address(dst_idx);
    
    test_copy_block(src_idx, dst_idx, 1);
    
    // Verify indexes are NOT modified
    uint32_t src_addr_after = get_index_address(src_idx);
    uint32_t dst_addr_after = get_index_address(dst_idx);
    if (src_addr_after != src_addr_before) {
        printf("FAIL: Source index was modified by copy_block(1) (expected 0x%06X, got 0x%06X)\n", 
               src_addr_before, src_addr_after);
        return false;
    }
    if (dst_addr_after != dst_addr_before) {
        printf("FAIL: Destination index was modified by copy_block(1) (expected 0x%06X, got 0x%06X)\n", 
               dst_addr_before, dst_addr_after);
        return false;
    }
    
    // Verify single byte was copied
    indexed_memory_execute_window_command(dst_idx, CMD_RESET_INDEX);
    uint8_t copied_byte = indexed_memory_read(dst_idx);
    if (copied_byte != test_data[0]) {
        printf("FAIL: Single byte copy (expected 0x%02X, got 0x%02X)\n", test_data[0], copied_byte);
        return false;
    }
    
    // Test multi-byte block copy
    indexed_memory_execute_window_command(src_idx, CMD_RESET_INDEX);
    indexed_memory_execute_window_command(dst_idx, CMD_RESET_INDEX);
    
    src_addr_before = get_index_address(src_idx);
    dst_addr_before = get_index_address(dst_idx);
    
    test_copy_block(src_idx, dst_idx, 5);
    
    // Verify indexes are NOT modified by copy operation
    src_addr_after = get_index_address(src_idx);
    dst_addr_after = get_index_address(dst_idx);
    if (src_addr_after != src_addr_before) {
        printf("FAIL: Source index was modified by copy_block (expected 0x%06X, got 0x%06X)\n", 
               src_addr_before, src_addr_after);
        return false;
    }
    if (dst_addr_after != dst_addr_before) {
        printf("FAIL: Destination index was modified by copy_block (expected 0x%06X, got 0x%06X)\n", 
               dst_addr_before, dst_addr_after);
        return false;
    }
    
    // Verify block copy
    indexed_memory_execute_window_command(dst_idx, CMD_RESET_INDEX);
    for (int i = 0; i < 5; i++) {
        uint8_t copied_data = indexed_memory_read(dst_idx);
        if (copied_data != test_data[i]) {
            printf("FAIL: Block copy at position %d (expected 0x%02X, got 0x%02X)\n", 
                   i, test_data[i], copied_data);
            return false;
        }
    }
    
    // Check DMA completion IRQ
    uint16_t irq_cause = test_get_irq_cause();
    if (!(irq_cause & IRQ_DMA_COMPLETE)) {
        printf("FAIL: DMA completion IRQ not set\n");
        return false;
    }
    
    // Clear IRQ for next test
    indexed_memory_execute_shared_command(CMD_CLEAR_IRQ);
    
    printf("PASS: DMA operations\n");
    return true;
}

/**
 * Test window management
 * 
 * Note: Window management is now handled by bus_interface module.
 * This test is kept as a placeholder but window-specific state
 * (active_index, config_field_select) is now tracked in the
 * window_state_t structure in bus_interface.c
 */
bool test_window_management(void) {
    printf("Testing window management...\n");
    printf("PASS: Window management (now handled by bus_interface module)\n");
    return true;
}

/**
 * Test error handling
 */
bool test_error_handling(void) {
    printf("Testing error handling...\n");
    
    uint8_t test_idx = IDX_USER_START + 5;
    
    // Test invalid address access
    // Use an address that's invalid even after 24-bit masking
    test_set_index_address(test_idx, 0x20080000); // 0x080000 after masking (beyond 256KB)
    test_set_index_flags(test_idx, 0); // No auto-step
    
    (void)indexed_memory_read(test_idx); // Suppress unused variable warning
    
    // Should have generated memory error
    uint8_t status = bus_interface_read(0xF0);  // REG_DEVICE_STATUS
    if (!(status & STATUS_MEMORY_ERROR)) {
        printf("FAIL: Memory error not detected\n");
        return false;
    }
    
    uint8_t irq_cause = test_get_irq_cause();
    if (irq_cause != IRQ_MEMORY_ERROR) {
        printf("FAIL: Memory error IRQ not set\n");
        return false;
    }
    
    // Clear error
    indexed_memory_execute_shared_command(CMD_CLEAR_IRQ);
    
    // Test address overflow
    test_set_index_address(test_idx, 0x2003FFF8); // Near end of memory (8 bytes before end)
    test_set_index_step(test_idx, 10);
    test_set_index_flags(test_idx, FLAG_AUTO_STEP);
    
    indexed_memory_write(test_idx, 0x55); // Write succeeds, but steps to invalid address
    
    // Now try to access the invalid address - should trigger error
    (void)indexed_memory_read(test_idx);
    
    status = bus_interface_read(0xF0);  // REG_DEVICE_STATUS
    if (!(status & STATUS_MEMORY_ERROR)) {
        printf("FAIL: Memory error not detected after overflow\n");
        return false;
    }
    
    irq_cause = test_get_irq_cause();
    if (!(irq_cause & IRQ_MEMORY_ERROR)) {
        printf("FAIL: Memory error IRQ not set after overflow\n");
        return false;
    }
    
    indexed_memory_execute_shared_command(CMD_CLEAR_IRQ);
    
    printf("PASS: Error handling\n");
    return true;
}

/**
 * Test wrap-on-limit functionality
 */
bool test_wrap_on_limit(void) {
    printf("Testing wrap-on-limit functionality...\n");
    
    uint8_t test_idx = IDX_USER_START + 6;
    
    // Configure index with wrap-on-limit for a 16-byte circular buffer
    uint32_t buffer_start = 0x20013C00; // MIA_USER_AREA_BASE + 0x400
    uint32_t buffer_limit = buffer_start + 16; // 16-byte buffer
    
    test_set_index_address(test_idx, buffer_start);
    test_set_index_default(test_idx, buffer_start);
    test_set_index_limit(test_idx, buffer_limit);
    test_set_index_step(test_idx, 1);
    test_set_index_flags(test_idx, FLAG_AUTO_STEP | FLAG_WRAP_ON_LIMIT);
    
    // Write 20 bytes (more than buffer size) to test wrapping
    uint8_t test_pattern[] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
                              0x88, 0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF,
                              0xF0, 0xF1, 0xF2, 0xF3}; // 20 bytes
    
    for (int i = 0; i < 20; i++) {
        indexed_memory_write(test_idx, test_pattern[i]);
    }
    
    // After writing 20 bytes to a 16-byte buffer with wrap, 
    // the last 4 bytes should have overwritten the first 4 bytes
    // Expected buffer content: [0xF0, 0xF1, 0xF2, 0xF3, 0x44, 0x55, ..., 0xFF]
    
    // Reset to start and verify
    indexed_memory_execute_window_command(test_idx, CMD_RESET_INDEX);
    
    // Check first 4 bytes (should be overwritten)
    uint8_t expected_first_4[] = {0xF0, 0xF1, 0xF2, 0xF3};
    for (int i = 0; i < 4; i++) {
        uint8_t read_data = indexed_memory_read(test_idx);
        if (read_data != expected_first_4[i]) {
            printf("FAIL: Wrap-on-limit first 4 bytes at position %d (expected 0x%02X, got 0x%02X)\n", 
                   i, expected_first_4[i], read_data);
            return false;
        }
    }
    
    // Check remaining bytes (should be original pattern bytes 4-15)
    for (int i = 4; i < 16; i++) {
        uint8_t read_data = indexed_memory_read(test_idx);
        if (read_data != test_pattern[i]) {
            printf("FAIL: Wrap-on-limit remaining bytes at position %d (expected 0x%02X, got 0x%02X)\n", 
                   i, test_pattern[i], read_data);
            return false;
        }
    }
    
    // Test that limit address configuration fields work
    indexed_memory_set_config_field(test_idx, CFG_LIMIT_L, 0xAB);
    indexed_memory_set_config_field(test_idx, CFG_LIMIT_M, 0xCD);
    indexed_memory_set_config_field(test_idx, CFG_LIMIT_H, 0xEF);
    
    if (indexed_memory_get_config_field(test_idx, CFG_LIMIT_L) != 0xAB ||
        indexed_memory_get_config_field(test_idx, CFG_LIMIT_M) != 0xCD ||
        indexed_memory_get_config_field(test_idx, CFG_LIMIT_H) != 0xEF) {
        printf("FAIL: Limit address field configuration\n");
        return false;
    }
    
    // Test wrap-on-limit with backward stepping
    test_set_index_address(test_idx, buffer_start + 2);
    test_set_index_default(test_idx, buffer_start + 10);
    test_set_index_limit(test_idx, buffer_start); // Limit at start (for backward wrap)
    test_set_index_step(test_idx, 3);
    test_set_index_flags(test_idx, FLAG_AUTO_STEP | FLAG_DIRECTION | FLAG_WRAP_ON_LIMIT);
    
    // Read backward - should wrap to default when going below limit
    indexed_memory_read(test_idx); // addr = buffer_start + 2 - 3 = buffer_start - 1 (below limit)
    
    // Should have wrapped to default address
    uint8_t addr_l = indexed_memory_get_config_field(test_idx, CFG_ADDR_L);
    uint8_t addr_m = indexed_memory_get_config_field(test_idx, CFG_ADDR_M);
    uint8_t addr_h = indexed_memory_get_config_field(test_idx, CFG_ADDR_H);
    
    uint32_t current_addr = addr_l | (addr_m << 8) | (addr_h << 16);
    uint32_t expected_addr = buffer_start + 10;
    
    if (current_addr != (expected_addr & 0xFFFFFF)) {
        printf("FAIL: Backward wrap-on-limit (expected 0x%06lX, got 0x%06lX)\n", 
               (unsigned long)(expected_addr & 0xFFFFFF), (unsigned long)current_addr);
        return false;
    }
    
    printf("PASS: Wrap-on-limit functionality\n");
    return true;
}

/**
 * Run all indexed memory tests
 */
bool run_indexed_memory_tests(void) {
    printf("\n=== MIA Indexed Memory System Tests ===\n");
    
    bool all_passed = true;
    
    all_passed &= test_indexed_memory_init();
    all_passed &= test_index_structure();
    all_passed &= test_basic_memory_access();
    all_passed &= test_auto_stepping();
    all_passed &= test_configuration_fields();
    all_passed &= test_dma_operations();
    all_passed &= test_window_management();
    all_passed &= test_error_handling();
    all_passed &= test_wrap_on_limit();
    
    printf("\n=== Test Results ===\n");
    if (all_passed) {
        printf("ALL TESTS PASSED\n");
    } else {
        printf("SOME TESTS FAILED\n");
    }
    
    return all_passed;
}