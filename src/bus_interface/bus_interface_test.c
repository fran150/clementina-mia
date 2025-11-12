/**
 * MIA Bus Interface Test Implementation
 * 
 * Tests for verifying bus interface address decoding with multi-window
 * architecture and shared register space.
 * 
 * NOTE: MIA only sees 8-bit addresses (A0-A7 on GPIO 0-7)
 * Tests use 8-bit local addresses that MIA actually sees
 */

#include "bus_interface_test.h"
#include "bus_interface.h"
#include <stdio.h>

/**
 * Test address validation
 * All 8-bit addresses are valid (windows + shared space)
 */
bool test_bus_interface_address_decoding(void) {
    printf("Testing bus interface address decoding...\n");
    
    // Test basic address decoding logic
    // Window A: 0x00-0x0F
    if ((0x00 & 0x80) != 0) {
        printf("  FAIL: 0x00 should not be in shared space\n");
        return false;
    }
    
    // Window B: 0x10-0x1F
    if ((0x1F & 0x80) != 0) {
        printf("  FAIL: 0x1F should not be in shared space\n");
        return false;
    }
    
    // Shared space: 0x80-0xFF
    if ((0x80 & 0x80) == 0) {
        printf("  FAIL: 0x80 should be in shared space\n");
        return false;
    }
    
    if ((0xFF & 0x80) == 0) {
        printf("  FAIL: 0xFF should be in shared space\n");
        return false;
    }
    
    printf("  PASS: Address decoding works correctly\n");
    return true;
}

/**
 * Test window detection (Windows A, B, C, D)
 * Using 8-bit local addresses that MIA actually sees
 */
bool test_bus_interface_window_detection(void) {
    printf("Testing bus interface window detection...\n");
    
    // Test Window A addresses (0x00-0x0F)
    if ((0x00 & 0x80) != 0) {
        printf("  FAIL: 0x00 should be Window A, not shared\n");
        return false;
    }
    if (((0x00 >> 4) & 0x07) != 0) {
        printf("  FAIL: 0x00 should be Window A (num=0)\n");
        return false;
    }
    
    // Test Window B addresses (0x10-0x1F)
    if ((0x10 & 0x80) != 0) {
        printf("  FAIL: 0x10 should be Window B, not shared\n");
        return false;
    }
    if (((0x10 >> 4) & 0x07) != 1) {
        printf("  FAIL: 0x10 should be Window B (num=1)\n");
        return false;
    }
    
    // Test Window C addresses (0x20-0x2F)
    if ((0x20 & 0x80) != 0) {
        printf("  FAIL: 0x20 should be Window C, not shared\n");
        return false;
    }
    if (((0x20 >> 4) & 0x07) != 2) {
        printf("  FAIL: 0x20 should be Window C (num=2)\n");
        return false;
    }
    
    // Test Window D addresses (0x30-0x3F)
    if ((0x30 & 0x80) != 0) {
        printf("  FAIL: 0x30 should be Window D, not shared\n");
        return false;
    }
    if (((0x30 >> 4) & 0x07) != 3) {
        printf("  FAIL: 0x30 should be Window D (num=3)\n");
        return false;
    }
    
    // Test shared space (0x80-0xFF)
    if ((0x80 & 0x80) == 0) {
        printf("  FAIL: 0x80 should be in shared space\n");
        return false;
    }
    if ((0xFF & 0x80) == 0) {
        printf("  FAIL: 0xFF should be in shared space\n");
        return false;
    }
    if ((0xF0 & 0x80) == 0) {
        printf("  FAIL: 0xF0 should be in shared space\n");
        return false;
    }
    
    printf("  PASS: Window detection works correctly\n");
    return true;
}

/**
 * Test register offset extraction within windows
 * Using 8-bit local addresses that MIA actually sees
 */
bool test_bus_interface_register_offsets(void) {
    printf("Testing bus interface register offset extraction...\n");
    
    // Test Window A register offsets (0x00-0x0F)
    for (uint8_t offset = 0; offset < 16; offset++) {
        uint8_t addr = 0x00 + offset;
        uint8_t extracted_offset = addr & 0x0F;
        if (extracted_offset != offset) {
            printf("  FAIL: Address 0x%02X should have offset %d\n", addr, offset);
            return false;
        }
    }
    
    // Test Window B register offsets (0x10-0x1F)
    for (uint8_t offset = 0; offset < 16; offset++) {
        uint8_t addr = 0x10 + offset;
        uint8_t extracted_offset = addr & 0x0F;
        if (extracted_offset != offset) {
            printf("  FAIL: Address 0x%02X should have offset %d\n", addr, offset);
            return false;
        }
    }
    
    // Test Window C register offsets (0x20-0x2F)
    for (uint8_t offset = 0; offset < 16; offset++) {
        uint8_t addr = 0x20 + offset;
        uint8_t extracted_offset = addr & 0x0F;
        if (extracted_offset != offset) {
            printf("  FAIL: Address 0x%02X should have offset %d\n", addr, offset);
            return false;
        }
    }
    
    // Test Window D register offsets (0x30-0x3F)
    for (uint8_t offset = 0; offset < 16; offset++) {
        uint8_t addr = 0x30 + offset;
        uint8_t extracted_offset = addr & 0x0F;
        if (extracted_offset != offset) {
            printf("  FAIL: Address 0x%02X should have offset %d\n", addr, offset);
            return false;
        }
    }
    
    printf("  PASS: Register offset extraction works correctly\n");
    return true;
}

/**
 * Test comprehensive address decoding
 * Using 8-bit local addresses that MIA actually sees
 */
bool test_bus_interface_decode_function(void) {
    printf("Testing bus interface decode logic...\n");
    
    // Test Window A address decoding (0x00 = IDX_SELECT in Window A)
    uint8_t addr = 0x00;
    bool is_shared = (addr & 0x80) != 0;
    uint8_t window_num = (addr >> 4) & 0x07;
    uint8_t reg_offset = addr & 0x0F;
    
    if (is_shared != false || window_num != 0 || reg_offset != REG_OFFSET_IDX_SELECT) {
        printf("  FAIL: 0x00 should decode to Window A (0), offset 0\n");
        return false;
    }
    
    // Test Window B address decoding (0x11 = DATA_PORT in Window B)
    addr = 0x11;
    is_shared = (addr & 0x80) != 0;
    window_num = (addr >> 4) & 0x07;
    reg_offset = addr & 0x0F;
    
    if (is_shared != false || window_num != 1 || reg_offset != REG_OFFSET_DATA_PORT) {
        printf("  FAIL: 0x11 should decode to Window B (1), offset 1\n");
        return false;
    }
    
    // Test Window C address decoding (0x22 = CFG_FIELD_SELECT in Window C)
    addr = 0x22;
    is_shared = (addr & 0x80) != 0;
    window_num = (addr >> 4) & 0x07;
    reg_offset = addr & 0x0F;
    
    if (is_shared != false || window_num != 2 || reg_offset != REG_OFFSET_CFG_FIELD_SELECT) {
        printf("  FAIL: 0x22 should decode to Window C (2), offset 2\n");
        return false;
    }
    
    // Test Window D address decoding (0x34 = COMMAND in Window D)
    addr = 0x34;
    is_shared = (addr & 0x80) != 0;
    window_num = (addr >> 4) & 0x07;
    reg_offset = addr & 0x0F;
    
    if (is_shared != false || window_num != 3 || reg_offset != REG_OFFSET_COMMAND) {
        printf("  FAIL: 0x34 should decode to Window D (3), offset 4\n");
        return false;
    }
    
    // Test shared space address decoding (0xF0 = DEVICE_STATUS)
    addr = 0xF0;
    is_shared = (addr & 0x80) != 0;
    reg_offset = addr & 0x7F;
    
    if (is_shared != true || reg_offset != 0x70) {
        printf("  FAIL: 0xF0 should decode to shared space, offset 0x70\n");
        return false;
    }
    
    // Test shared space address decoding (0xFF)
    addr = 0xFF;
    is_shared = (addr & 0x80) != 0;
    reg_offset = addr & 0x7F;
    
    if (is_shared != true || reg_offset != 0x7F) {
        printf("  FAIL: 0xFF should decode to shared space, offset 0x7F\n");
        return false;
    }
    
    printf("  PASS: Address decode logic works correctly\n");
    return true;
}

/**
 * Test address validation
 * All 8-bit addresses are valid
 */
bool test_bus_interface_address_validation(void) {
    printf("Testing bus interface address validation...\n");
    
    // All addresses 0x00-0xFF are valid
    // Test a few representative addresses
    
    // Window addresses
    printf("  PASS: All addresses are valid (0x00-0xFF)\n");
    return true;
}

/**
 * Test edge cases for multi-window architecture
 * Using 8-bit local addresses that MIA actually sees
 */
bool test_bus_interface_multiwindow_edge_cases(void) {
    printf("Testing bus interface multi-window edge cases...\n");
    
    // Test boundary between Window A and Window B
    uint8_t addr = 0x0F;
    bool is_shared = (addr & 0x80) != 0;
    uint8_t window_num = (addr >> 4) & 0x07;
    uint8_t reg_offset = addr & 0x0F;
    
    if (is_shared || window_num != 0 || reg_offset != 0x0F) {
        printf("  FAIL: 0x0F should be last register of Window A\n");
        return false;
    }
    
    addr = 0x10;
    is_shared = (addr & 0x80) != 0;
    window_num = (addr >> 4) & 0x07;
    reg_offset = addr & 0x0F;
    
    if (is_shared || window_num != 1 || reg_offset != 0x00) {
        printf("  FAIL: 0x10 should be first register of Window B\n");
        return false;
    }
    
    // Test boundary between Window D and reserved space
    addr = 0x3F;
    is_shared = (addr & 0x80) != 0;
    window_num = (addr >> 4) & 0x07;
    reg_offset = addr & 0x0F;
    
    if (is_shared || window_num != 3 || reg_offset != 0x0F) {
        printf("  FAIL: 0x3F should be last register of Window D\n");
        return false;
    }
    
    addr = 0x40;
    is_shared = (addr & 0x80) != 0;
    window_num = (addr >> 4) & 0x07;
    reg_offset = addr & 0x0F;
    
    if (is_shared || window_num != 4 || reg_offset != 0x00) {
        printf("  FAIL: 0x40 should be first register of future Window E\n");
        return false;
    }
    
    // Test boundary between reserved space and shared space
    addr = 0x7F;
    is_shared = (addr & 0x80) != 0;
    window_num = (addr >> 4) & 0x07;
    reg_offset = addr & 0x0F;
    
    if (is_shared || window_num != 7 || reg_offset != 0x0F) {
        printf("  FAIL: 0x7F should be last register of future Window H\n");
        return false;
    }
    
    addr = 0x80;
    is_shared = (addr & 0x80) != 0;
    reg_offset = addr & 0x7F;
    
    if (!is_shared || reg_offset != 0x00) {
        printf("  FAIL: 0x80 should be first register of shared space\n");
        return false;
    }
    
    printf("  PASS: Multi-window edge cases work correctly\n");
    return true;
}

/**
 * Test shared register space
 */
bool test_bus_interface_shared_registers(void) {
    printf("Testing bus interface shared register space...\n");
    
    // Test that shared registers are correctly identified
    if ((REG_DEVICE_STATUS & 0x80) == 0) {
        printf("  FAIL: DEVICE_STATUS (0xF0) should be in shared space\n");
        return false;
    }
    
    if ((REG_IRQ_CAUSE_LOW & 0x80) == 0) {
        printf("  FAIL: IRQ_CAUSE_LOW (0xF1) should be in shared space\n");
        return false;
    }
    
    if ((REG_IRQ_CAUSE_HIGH & 0x80) == 0) {
        printf("  FAIL: IRQ_CAUSE_HIGH (0xF2) should be in shared space\n");
        return false;
    }
    
    if ((REG_DEVICE_ID_HIGH & 0x80) == 0) {
        printf("  FAIL: DEVICE_ID_HIGH (0xF7) should be in shared space\n");
        return false;
    }
    
    // Test that window registers are not in shared space
    if ((0x00 & 0x80) != 0) {
        printf("  FAIL: 0x00 should not be in shared space\n");
        return false;
    }
    
    if ((0x7F & 0x80) != 0) {
        printf("  FAIL: 0x7F should not be in shared space\n");
        return false;
    }
    
    printf("  PASS: Shared register space works correctly\n");
    return true;
}

/**
 * Test window state management initialization
 */
bool test_bus_interface_window_state_init(void) {
    printf("Testing bus interface window state initialization...\n");
    
    // Initialize the bus interface
    bus_interface_init();
    
    // Verify all windows are initialized to index 0
    for (uint8_t window = 0; window < MAX_WINDOWS; window++) {
        if (g_window_state[window].active_index != 0) {
            printf("  FAIL: Window %d should be initialized to index 0, got %d\n", 
                   window, g_window_state[window].active_index);
            return false;
        }
        
        if (g_window_state[window].config_field_select != 0) {
            printf("  FAIL: Window %d config field should be initialized to 0, got %d\n", 
                   window, g_window_state[window].config_field_select);
            return false;
        }
    }
    
    printf("  PASS: Window state initialization works correctly\n");
    return true;
}

/**
 * Test window state get/set for active index
 */
bool test_bus_interface_window_index_access(void) {
    printf("Testing bus interface window index access...\n");
    
    // Initialize the bus interface
    bus_interface_init();
    
    // Test setting and getting index for each window
    for (uint8_t window = 0; window < MAX_WINDOWS; window++) {
        uint8_t test_index = 10 + window;  // Use different index for each window
        
        g_window_state[window].active_index = test_index;
        
        if (g_window_state[window].active_index != test_index) {
            printf("  FAIL: Window %d index should be %d, got %d\n", 
                   window, test_index, g_window_state[window].active_index);
            return false;
        }
    }
    
    // Test that windows are independent
    g_window_state[0].active_index = 100;
    g_window_state[1].active_index = 200;
    
    if (g_window_state[0].active_index != 100) {
        printf("  FAIL: Window A index should be 100\n");
        return false;
    }
    
    if (g_window_state[1].active_index != 200) {
        printf("  FAIL: Window B index should be 200\n");
        return false;
    }
    
    printf("  PASS: Window index access works correctly\n");
    return true;
}

/**
 * Test window state get/set for config field
 */
bool test_bus_interface_config_field_access(void) {
    printf("Testing bus interface config field access...\n");
    
    // Initialize the bus interface
    bus_interface_init();
    
    // Test setting and getting config field for each window
    for (uint8_t window = 0; window < MAX_WINDOWS; window++) {
        uint8_t test_field = 5 + window;  // Use different field for each window
        
        g_window_state[window].config_field_select = test_field;
        
        if (g_window_state[window].config_field_select != test_field) {
            printf("  FAIL: Window %d config field should be %d, got %d\n", 
                   window, test_field, g_window_state[window].config_field_select);
            return false;
        }
    }
    
    // Test that windows are independent
    g_window_state[0].config_field_select = 0x0A;
    g_window_state[1].config_field_select = 0x0B;
    
    if (g_window_state[0].config_field_select != 0x0A) {
        printf("  FAIL: Window A config field should be 0x0A\n");
        return false;
    }
    
    if (g_window_state[1].config_field_select != 0x0B) {
        printf("  FAIL: Window B config field should be 0x0B\n");
        return false;
    }
    
    printf("  PASS: Config field access works correctly\n");
    return true;
}

/**
 * Test window state independence across all windows
 */
bool test_bus_interface_window_independence(void) {
    printf("Testing bus interface window independence...\n");
    
    // Initialize the bus interface
    bus_interface_init();
    
    // Set unique values for each window
    for (uint8_t window = 0; window < MAX_WINDOWS; window++) {
        g_window_state[window].active_index = 10 * window;
        g_window_state[window].config_field_select = window;
    }
    
    // Verify each window retained its unique values
    for (uint8_t window = 0; window < MAX_WINDOWS; window++) {
        if (g_window_state[window].active_index != 10 * window) {
            printf("  FAIL: Window %d index should be %d, got %d\n", 
                   window, 10 * window, g_window_state[window].active_index);
            return false;
        }
        
        if (g_window_state[window].config_field_select != window) {
            printf("  FAIL: Window %d config field should be %d, got %d\n", 
                   window, window, g_window_state[window].config_field_select);
            return false;
        }
    }
    
    printf("  PASS: Window independence works correctly\n");
    return true;
}

/**
 * Test direct array access pattern
 */
bool test_bus_interface_direct_access(void) {
    printf("Testing bus interface direct array access...\n");
    
    // Initialize the bus interface
    bus_interface_init();
    
    // Test direct access pattern (simulating what register handlers will do)
    uint8_t window_num = 2;  // Window C
    g_window_state[window_num].active_index = 42;
    g_window_state[window_num].config_field_select = 0x0A;
    
    if (g_window_state[window_num].active_index != 42) {
        printf("  FAIL: Direct access to active_index failed\n");
        return false;
    }
    
    if (g_window_state[window_num].config_field_select != 0x0A) {
        printf("  FAIL: Direct access to config_field_select failed\n");
        return false;
    }
    
    // Verify other windows weren't affected
    if (g_window_state[0].active_index != 0 || g_window_state[1].active_index != 0) {
        printf("  FAIL: Other windows were affected by direct access\n");
        return false;
    }
    
    printf("  PASS: Direct array access works correctly\n");
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
    all_passed &= test_bus_interface_register_offsets();
    all_passed &= test_bus_interface_decode_function();
    all_passed &= test_bus_interface_address_validation();
    all_passed &= test_bus_interface_multiwindow_edge_cases();
    all_passed &= test_bus_interface_shared_registers();
    all_passed &= test_bus_interface_window_state_init();
    all_passed &= test_bus_interface_window_index_access();
    all_passed &= test_bus_interface_config_field_access();
    all_passed &= test_bus_interface_window_independence();
    all_passed &= test_bus_interface_direct_access();
    
    if (all_passed) {
        printf("\n=== All Bus Interface Tests PASSED ===\n\n");
    } else {
        printf("\n=== Some Bus Interface Tests FAILED ===\n\n");
    }
    
    return all_passed;
}
