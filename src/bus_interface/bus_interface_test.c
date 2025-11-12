/**
 * MIA Bus Interface Test Implementation
 * 
 * Tests for verifying bus interface address decoding and window detection
 */

#include "bus_interface_test.h"
#include "bus_interface.h"
#include <stdio.h>

/**
 * Test address decoding and window detection
 */
bool test_bus_interface_address_decoding(void) {
    printf("Testing bus interface address decoding...\n");
    
    // Test indexed interface address detection
    if (!bus_interface_is_indexed_addr(0xC000)) {
        printf("  FAIL: 0xC000 should be in indexed interface range\n");
        return false;
    }
    
    if (!bus_interface_is_indexed_addr(0xC3FF)) {
        printf("  FAIL: 0xC3FF should be in indexed interface range\n");
        return false;
    }
    
    if (bus_interface_is_indexed_addr(0xBFFF)) {
        printf("  FAIL: 0xBFFF should NOT be in indexed interface range\n");
        return false;
    }
    
    if (bus_interface_is_indexed_addr(0xC400)) {
        printf("  FAIL: 0xC400 should NOT be in indexed interface range\n");
        return false;
    }
    
    printf("  PASS: Address range detection works correctly\n");
    return true;
}

/**
 * Test window detection (Window A vs Window B)
 */
bool test_bus_interface_window_detection(void) {
    printf("Testing bus interface window detection...\n");
    
    // Test Window A addresses (bit 3 = 0, offsets 0-7)
    if (bus_interface_is_window_b(0xC000)) {
        printf("  FAIL: 0xC000 should be Window A\n");
        return false;
    }
    
    if (bus_interface_is_window_b(0xC007)) {
        printf("  FAIL: 0xC007 should be Window A\n");
        return false;
    }
    
    // Test Window B addresses (bit 3 = 1, offsets 8-15)
    if (!bus_interface_is_window_b(0xC008)) {
        printf("  FAIL: 0xC008 should be Window B\n");
        return false;
    }
    
    if (!bus_interface_is_window_b(0xC00F)) {
        printf("  FAIL: 0xC00F should be Window B\n");
        return false;
    }
    
    printf("  PASS: Window detection works correctly\n");
    return true;
}

/**
 * Test register address mirroring throughout $C000-$C3FF
 */
bool test_bus_interface_register_mirroring(void) {
    printf("Testing bus interface register mirroring...\n");
    
    // Test that register offsets are correctly extracted with mirroring
    // IDX_SELECT_A at 0xC000 should mirror to 0xC010, 0xC020, etc.
    if (bus_interface_get_register_offset(0xC000) != REG_OFFSET_IDX_SELECT) {
        printf("  FAIL: 0xC000 should map to IDX_SELECT offset\n");
        return false;
    }
    
    if (bus_interface_get_register_offset(0xC010) != REG_OFFSET_IDX_SELECT) {
        printf("  FAIL: 0xC010 should mirror to IDX_SELECT offset\n");
        return false;
    }
    
    if (bus_interface_get_register_offset(0xC100) != REG_OFFSET_IDX_SELECT) {
        printf("  FAIL: 0xC100 should mirror to IDX_SELECT offset\n");
        return false;
    }
    
    // Test DATA_PORT mirroring
    if (bus_interface_get_register_offset(0xC001) != REG_OFFSET_DATA_PORT) {
        printf("  FAIL: 0xC001 should map to DATA_PORT offset\n");
        return false;
    }
    
    if (bus_interface_get_register_offset(0xC3F1) != REG_OFFSET_DATA_PORT) {
        printf("  FAIL: 0xC3F1 should mirror to DATA_PORT offset\n");
        return false;
    }
    
    // Test Window B register mirroring
    if (bus_interface_get_register_offset(0xC008) != REG_OFFSET_IDX_SELECT) {
        printf("  FAIL: 0xC008 should map to IDX_SELECT offset (Window B)\n");
        return false;
    }
    
    if (bus_interface_get_register_offset(0xC018) != REG_OFFSET_IDX_SELECT) {
        printf("  FAIL: 0xC018 should mirror to IDX_SELECT offset (Window B)\n");
        return false;
    }
    
    printf("  PASS: Register mirroring works correctly\n");
    return true;
}

/**
 * Run all bus interface tests
 */
bool run_bus_interface_tests(void) {
    printf("\n=== Running Bus Interface Tests ===\n");
    
    bool all_passed = true;
    
    all_passed &= test_bus_interface_address_decoding();
    all_passed &= test_bus_interface_window_detection();
    all_passed &= test_bus_interface_register_mirroring();
    
    if (all_passed) {
        printf("\n=== All Bus Interface Tests PASSED ===\n\n");
    } else {
        printf("\n=== Some Bus Interface Tests FAILED ===\n\n");
    }
    
    return all_passed;
}
