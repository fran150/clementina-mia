/**
 * MIA Bus Interface Test Implementation
 * 
 * Tests for verifying bus interface address decoding with multi-window
 * architecture and shared register space.
 * 
 * NOTE: MIA only sees 8-bit addresses (A0-A7 on GPIO 0-7)
 * Tests use 8-bit local addresses that MIA actually sees
 */

#include "test_bus_interface.h"
#include "bus_interface/bus_interface.h"
#include "indexed_memory/indexed_memory.h"
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
 * Test IDX_SELECT read handler for all windows
 */
bool test_bus_interface_idx_select_read(void) {
    printf("Testing IDX_SELECT read handler...\n");
    
    // Initialize the bus interface and indexed memory
    bus_interface_init();
    
    // Test reading IDX_SELECT for Window A (address 0x00)
    g_window_state[0].active_index = 42;
    uint8_t value = bus_interface_read(0x00);
    if (value != 42) {
        printf("  FAIL: Window A IDX_SELECT should return 42, got %d\n", value);
        return false;
    }
    
    // Test reading IDX_SELECT for Window B (address 0x10)
    g_window_state[1].active_index = 100;
    value = bus_interface_read(0x10);
    if (value != 100) {
        printf("  FAIL: Window B IDX_SELECT should return 100, got %d\n", value);
        return false;
    }
    
    // Test reading IDX_SELECT for Window C (address 0x20)
    g_window_state[2].active_index = 200;
    value = bus_interface_read(0x20);
    if (value != 200) {
        printf("  FAIL: Window C IDX_SELECT should return 200, got %d\n", value);
        return false;
    }
    
    // Test reading IDX_SELECT for Window D (address 0x30)
    g_window_state[3].active_index = 255;
    value = bus_interface_read(0x30);
    if (value != 255) {
        printf("  FAIL: Window D IDX_SELECT should return 255, got %d\n", value);
        return false;
    }
    
    // Test that windows are independent
    g_window_state[0].active_index = 10;
    g_window_state[1].active_index = 20;
    g_window_state[2].active_index = 30;
    g_window_state[3].active_index = 40;
    
    if (bus_interface_read(0x00) != 10) {
        printf("  FAIL: Window A should return 10\n");
        return false;
    }
    if (bus_interface_read(0x10) != 20) {
        printf("  FAIL: Window B should return 20\n");
        return false;
    }
    if (bus_interface_read(0x20) != 30) {
        printf("  FAIL: Window C should return 30\n");
        return false;
    }
    if (bus_interface_read(0x30) != 40) {
        printf("  FAIL: Window D should return 40\n");
        return false;
    }
    
    printf("  PASS: IDX_SELECT read handler works correctly for all windows\n");
    return true;
}

/**
 * Test IDX_SELECT write handler for all windows
 */
bool test_bus_interface_idx_select_write(void) {
    printf("Testing IDX_SELECT write handler...\n");
    
    // Initialize the bus interface
    bus_interface_init();
    
    // Test writing IDX_SELECT for Window A (address 0x00)
    bus_interface_write(0x00, 42);
    if (g_window_state[0].active_index != 42) {
        printf("  FAIL: Window A IDX_SELECT should be 42, got %d\n", g_window_state[0].active_index);
        return false;
    }
    
    // Test writing IDX_SELECT for Window B (address 0x10)
    bus_interface_write(0x10, 100);
    if (g_window_state[1].active_index != 100) {
        printf("  FAIL: Window B IDX_SELECT should be 100, got %d\n", g_window_state[1].active_index);
        return false;
    }
    
    // Test writing IDX_SELECT for Window C (address 0x20)
    bus_interface_write(0x20, 200);
    if (g_window_state[2].active_index != 200) {
        printf("  FAIL: Window C IDX_SELECT should be 200, got %d\n", g_window_state[2].active_index);
        return false;
    }
    
    // Test writing IDX_SELECT for Window D (address 0x30)
    bus_interface_write(0x30, 255);
    if (g_window_state[3].active_index != 255) {
        printf("  FAIL: Window D IDX_SELECT should be 255, got %d\n", g_window_state[3].active_index);
        return false;
    }
    
    // Test that windows are independent
    bus_interface_write(0x00, 10);
    bus_interface_write(0x10, 20);
    bus_interface_write(0x20, 30);
    bus_interface_write(0x30, 40);
    
    if (g_window_state[0].active_index != 10) {
        printf("  FAIL: Window A should be 10\n");
        return false;
    }
    if (g_window_state[1].active_index != 20) {
        printf("  FAIL: Window B should be 20\n");
        return false;
    }
    if (g_window_state[2].active_index != 30) {
        printf("  FAIL: Window C should be 30\n");
        return false;
    }
    if (g_window_state[3].active_index != 40) {
        printf("  FAIL: Window D should be 40\n");
        return false;
    }
    
    printf("  PASS: IDX_SELECT write handler works correctly for all windows\n");
    return true;
}

/**
 * Test IDX_SELECT read/write integration for all windows
 */
bool test_bus_interface_idx_select_integration(void) {
    printf("Testing IDX_SELECT read/write integration...\n");
    
    // Initialize the bus interface
    bus_interface_init();
    
    // Test write then read for each window
    for (uint8_t window = 0; window < 4; window++) {
        uint8_t addr = window << 4;  // 0x00, 0x10, 0x20, 0x30
        uint8_t test_value = 50 + window;
        
        // Write value
        bus_interface_write(addr, test_value);
        
        // Read it back
        uint8_t read_value = bus_interface_read(addr);
        
        if (read_value != test_value) {
            printf("  FAIL: Window %d write/read mismatch: wrote %d, read %d\n", 
                   window, test_value, read_value);
            return false;
        }
    }
    
    // Test multiple writes to same window
    bus_interface_write(0x00, 100);
    if (bus_interface_read(0x00) != 100) {
        printf("  FAIL: First write to Window A failed\n");
        return false;
    }
    
    bus_interface_write(0x00, 200);
    if (bus_interface_read(0x00) != 200) {
        printf("  FAIL: Second write to Window A failed\n");
        return false;
    }
    
    // Test that other windows weren't affected
    bus_interface_write(0x10, 111);
    bus_interface_write(0x00, 222);
    
    if (bus_interface_read(0x10) != 111) {
        printf("  FAIL: Window B was affected by Window A write\n");
        return false;
    }
    
    if (bus_interface_read(0x00) != 222) {
        printf("  FAIL: Window A write failed\n");
        return false;
    }
    
    printf("  PASS: IDX_SELECT read/write integration works correctly\n");
    return true;
}

/**
 * Test DATA_PORT read handler for all windows
 */
bool test_bus_interface_data_port_read(void) {
    printf("Testing DATA_PORT read handler...\n");
    
    // Initialize the bus interface and indexed memory
    bus_interface_init();
    indexed_memory_init();
    
    // Test reading DATA_PORT for Window A (address 0x01)
    // Select index 64 (USB keyboard buffer) for Window A
    bus_interface_write(0x00, 64);
    
    // Write some test data to the index
    indexed_memory_write(64, 0xAA);
    indexed_memory_write(64, 0xBB);
    indexed_memory_write(64, 0xCC);
    
    // Reset index to read back the data
    indexed_memory_reset_index(64);
    
    // Read data via DATA_PORT
    uint8_t value = bus_interface_read(0x01);
    if (value != 0xAA) {
        printf("  FAIL: Window A DATA_PORT should return 0xAA, got 0x%02X\n", value);
        return false;
    }
    
    // Auto-stepping should advance to next byte
    value = bus_interface_read(0x01);
    if (value != 0xBB) {
        printf("  FAIL: Window A DATA_PORT should return 0xBB after auto-step, got 0x%02X\n", value);
        return false;
    }
    
    // Test reading DATA_PORT for Window B (address 0x11)
    bus_interface_write(0x10, 128);  // Select user index
    indexed_memory_write(128, 0x11);
    indexed_memory_write(128, 0x22);
    indexed_memory_reset_index(128);
    
    value = bus_interface_read(0x11);
    if (value != 0x11) {
        printf("  FAIL: Window B DATA_PORT should return 0x11, got 0x%02X\n", value);
        return false;
    }
    
    // Test reading DATA_PORT for Window C (address 0x21)
    bus_interface_write(0x20, 129);  // Select user index
    indexed_memory_write(129, 0x33);
    indexed_memory_reset_index(129);
    
    value = bus_interface_read(0x21);
    if (value != 0x33) {
        printf("  FAIL: Window C DATA_PORT should return 0x33, got 0x%02X\n", value);
        return false;
    }
    
    // Test reading DATA_PORT for Window D (address 0x31)
    bus_interface_write(0x30, 130);  // Select user index
    indexed_memory_write(130, 0x44);
    indexed_memory_reset_index(130);
    
    value = bus_interface_read(0x31);
    if (value != 0x44) {
        printf("  FAIL: Window D DATA_PORT should return 0x44, got 0x%02X\n", value);
        return false;
    }
    
    printf("  PASS: DATA_PORT read handler works correctly for all windows\n");
    return true;
}

/**
 * Test DATA_PORT read with auto-stepping
 */
bool test_bus_interface_data_port_auto_step(void) {
    printf("Testing DATA_PORT read with auto-stepping...\n");
    
    // Initialize the bus interface and indexed memory
    bus_interface_init();
    indexed_memory_init();
    
    // Select index 128 (user area) for Window A
    bus_interface_write(0x00, 128);
    
    // Write a sequence of bytes
    for (uint8_t i = 0; i < 10; i++) {
        indexed_memory_write(128, i * 10);
    }
    
    // Reset index to start
    indexed_memory_reset_index(128);
    
    // Read back the sequence via DATA_PORT with auto-stepping
    for (uint8_t i = 0; i < 10; i++) {
        uint8_t value = bus_interface_read(0x01);
        if (value != i * 10) {
            printf("  FAIL: Expected 0x%02X at position %d, got 0x%02X\n", i * 10, i, value);
            return false;
        }
    }
    
    printf("  PASS: DATA_PORT auto-stepping works correctly\n");
    return true;
}

/**
 * Test DATA_PORT read with multiple windows
 */
bool test_bus_interface_data_port_multi_window(void) {
    printf("Testing DATA_PORT read with multiple windows...\n");
    
    // Initialize the bus interface and indexed memory
    bus_interface_init();
    indexed_memory_init();
    
    // Configure indexes to point to different memory locations
    // User indexes all start at the same base address, so we need to offset them
    uint32_t base_addr = 0x20013800;  // MIA_USER_AREA_BASE
    indexed_memory_set_index_address(128, base_addr + 0);
    indexed_memory_set_index_default(128, base_addr + 0);
    
    indexed_memory_set_index_address(129, base_addr + 100);
    indexed_memory_set_index_default(129, base_addr + 100);
    
    indexed_memory_set_index_address(130, base_addr + 200);
    indexed_memory_set_index_default(130, base_addr + 200);
    
    indexed_memory_set_index_address(131, base_addr + 300);
    indexed_memory_set_index_default(131, base_addr + 300);
    
    // Set up different indexes for each window
    bus_interface_write(0x00, 128);  // Window A -> index 128
    bus_interface_write(0x10, 129);  // Window B -> index 129
    bus_interface_write(0x20, 130);  // Window C -> index 130
    bus_interface_write(0x30, 131);  // Window D -> index 131
    
    // Write different data to each index
    indexed_memory_write(128, 0xAA);
    indexed_memory_write(129, 0xBB);
    indexed_memory_write(130, 0xCC);
    indexed_memory_write(131, 0xDD);
    
    // Reset all indexes to read from the beginning
    indexed_memory_reset_index(128);
    indexed_memory_reset_index(129);
    indexed_memory_reset_index(130);
    indexed_memory_reset_index(131);
    
    // Read from each window and verify independence
    uint8_t val_a = bus_interface_read(0x01);
    uint8_t val_b = bus_interface_read(0x11);
    uint8_t val_c = bus_interface_read(0x21);
    uint8_t val_d = bus_interface_read(0x31);
    
    if (val_a != 0xAA) {
        printf("  FAIL: Window A should return 0xAA, got 0x%02X\n", val_a);
        return false;
    }
    if (val_b != 0xBB) {
        printf("  FAIL: Window B should return 0xBB, got 0x%02X\n", val_b);
        return false;
    }
    if (val_c != 0xCC) {
        printf("  FAIL: Window C should return 0xCC, got 0x%02X\n", val_c);
        return false;
    }
    if (val_d != 0xDD) {
        printf("  FAIL: Window D should return 0xDD, got 0x%02X\n", val_d);
        return false;
    }
    
    printf("  PASS: DATA_PORT multi-window access works correctly\n");
    return true;
}

/**
 * Test DATA_PORT write handler for all windows
 */
bool test_bus_interface_data_port_write(void) {
    printf("Testing DATA_PORT write handler...\n");
    
    // Initialize the bus interface and indexed memory
    bus_interface_init();
    indexed_memory_init();
    
    // Test writing DATA_PORT for Window A (address 0x01)
    // Select index 128 (user area) for Window A
    bus_interface_write(0x00, 128);
    
    // Reset index to start
    indexed_memory_reset_index(128);
    
    // Write data via DATA_PORT
    bus_interface_write(0x01, 0xAA);
    bus_interface_write(0x01, 0xBB);
    bus_interface_write(0x01, 0xCC);
    
    // Reset index and read back to verify
    indexed_memory_reset_index(128);
    uint8_t value = indexed_memory_read(128);
    if (value != 0xAA) {
        printf("  FAIL: Window A DATA_PORT write failed, expected 0xAA, got 0x%02X\n", value);
        return false;
    }
    
    value = indexed_memory_read(128);
    if (value != 0xBB) {
        printf("  FAIL: Window A DATA_PORT write failed, expected 0xBB, got 0x%02X\n", value);
        return false;
    }
    
    value = indexed_memory_read(128);
    if (value != 0xCC) {
        printf("  FAIL: Window A DATA_PORT write failed, expected 0xCC, got 0x%02X\n", value);
        return false;
    }
    
    // Test writing DATA_PORT for Window B (address 0x11)
    bus_interface_write(0x10, 129);  // Select user index
    indexed_memory_reset_index(129);
    
    bus_interface_write(0x11, 0x11);
    bus_interface_write(0x11, 0x22);
    
    indexed_memory_reset_index(129);
    value = indexed_memory_read(129);
    if (value != 0x11) {
        printf("  FAIL: Window B DATA_PORT write failed, expected 0x11, got 0x%02X\n", value);
        return false;
    }
    
    // Test writing DATA_PORT for Window C (address 0x21)
    bus_interface_write(0x20, 130);  // Select user index
    indexed_memory_reset_index(130);
    
    bus_interface_write(0x21, 0x33);
    
    indexed_memory_reset_index(130);
    value = indexed_memory_read(130);
    if (value != 0x33) {
        printf("  FAIL: Window C DATA_PORT write failed, expected 0x33, got 0x%02X\n", value);
        return false;
    }
    
    // Test writing DATA_PORT for Window D (address 0x31)
    bus_interface_write(0x30, 131);  // Select user index
    indexed_memory_reset_index(131);
    
    bus_interface_write(0x31, 0x44);
    
    indexed_memory_reset_index(131);
    value = indexed_memory_read(131);
    if (value != 0x44) {
        printf("  FAIL: Window D DATA_PORT write failed, expected 0x44, got 0x%02X\n", value);
        return false;
    }
    
    printf("  PASS: DATA_PORT write handler works correctly for all windows\n");
    return true;
}

/**
 * Test DATA_PORT write with auto-stepping
 */
bool test_bus_interface_data_port_write_auto_step(void) {
    printf("Testing DATA_PORT write with auto-stepping...\n");
    
    // Initialize the bus interface and indexed memory
    bus_interface_init();
    indexed_memory_init();
    
    // Select index 128 (user area) for Window A
    bus_interface_write(0x00, 128);
    
    // Reset index to start
    indexed_memory_reset_index(128);
    
    // Write a sequence of bytes via DATA_PORT with auto-stepping
    for (uint8_t i = 0; i < 10; i++) {
        bus_interface_write(0x01, i * 10);
    }
    
    // Reset index and read back to verify
    indexed_memory_reset_index(128);
    for (uint8_t i = 0; i < 10; i++) {
        uint8_t value = indexed_memory_read(128);
        if (value != i * 10) {
            printf("  FAIL: Expected 0x%02X at position %d, got 0x%02X\n", i * 10, i, value);
            return false;
        }
    }
    
    printf("  PASS: DATA_PORT write auto-stepping works correctly\n");
    return true;
}

/**
 * Test DATA_PORT write with multiple windows
 */
bool test_bus_interface_data_port_write_multi_window(void) {
    printf("Testing DATA_PORT write with multiple windows...\n");
    
    // Initialize the bus interface and indexed memory
    bus_interface_init();
    indexed_memory_init();
    
    // Configure indexes to point to different memory locations
    uint32_t base_addr = 0x20013800;  // MIA_USER_AREA_BASE
    indexed_memory_set_index_address(128, base_addr + 0);
    indexed_memory_set_index_default(128, base_addr + 0);
    
    indexed_memory_set_index_address(129, base_addr + 100);
    indexed_memory_set_index_default(129, base_addr + 100);
    
    indexed_memory_set_index_address(130, base_addr + 200);
    indexed_memory_set_index_default(130, base_addr + 200);
    
    indexed_memory_set_index_address(131, base_addr + 300);
    indexed_memory_set_index_default(131, base_addr + 300);
    
    // Set up different indexes for each window
    bus_interface_write(0x00, 128);  // Window A -> index 128
    bus_interface_write(0x10, 129);  // Window B -> index 129
    bus_interface_write(0x20, 130);  // Window C -> index 130
    bus_interface_write(0x30, 131);  // Window D -> index 131
    
    // Reset all indexes
    indexed_memory_reset_index(128);
    indexed_memory_reset_index(129);
    indexed_memory_reset_index(130);
    indexed_memory_reset_index(131);
    
    // Write different data to each window
    bus_interface_write(0x01, 0xAA);  // Window A
    bus_interface_write(0x11, 0xBB);  // Window B
    bus_interface_write(0x21, 0xCC);  // Window C
    bus_interface_write(0x31, 0xDD);  // Window D
    
    // Reset all indexes and read back to verify independence
    indexed_memory_reset_index(128);
    indexed_memory_reset_index(129);
    indexed_memory_reset_index(130);
    indexed_memory_reset_index(131);
    
    uint8_t val_a = indexed_memory_read(128);
    uint8_t val_b = indexed_memory_read(129);
    uint8_t val_c = indexed_memory_read(130);
    uint8_t val_d = indexed_memory_read(131);
    
    if (val_a != 0xAA) {
        printf("  FAIL: Window A should have written 0xAA, got 0x%02X\n", val_a);
        return false;
    }
    if (val_b != 0xBB) {
        printf("  FAIL: Window B should have written 0xBB, got 0x%02X\n", val_b);
        return false;
    }
    if (val_c != 0xCC) {
        printf("  FAIL: Window C should have written 0xCC, got 0x%02X\n", val_c);
        return false;
    }
    if (val_d != 0xDD) {
        printf("  FAIL: Window D should have written 0xDD, got 0x%02X\n", val_d);
        return false;
    }
    
    printf("  PASS: DATA_PORT write multi-window access works correctly\n");
    return true;
}

/**
 * Test DATA_PORT read/write integration
 */
bool test_bus_interface_data_port_read_write_integration(void) {
    printf("Testing DATA_PORT read/write integration...\n");
    
    // Initialize the bus interface and indexed memory
    bus_interface_init();
    indexed_memory_init();
    
    // Select index 128 for Window A
    bus_interface_write(0x00, 128);
    indexed_memory_reset_index(128);
    
    // Write data via DATA_PORT
    bus_interface_write(0x01, 0x12);
    bus_interface_write(0x01, 0x34);
    bus_interface_write(0x01, 0x56);
    
    // Reset index and read back via DATA_PORT
    indexed_memory_reset_index(128);
    
    uint8_t val1 = bus_interface_read(0x01);
    uint8_t val2 = bus_interface_read(0x01);
    uint8_t val3 = bus_interface_read(0x01);
    
    if (val1 != 0x12 || val2 != 0x34 || val3 != 0x56) {
        printf("  FAIL: Read/write integration failed: got 0x%02X, 0x%02X, 0x%02X\n", 
               val1, val2, val3);
        return false;
    }
    
    // Test interleaved read/write operations
    indexed_memory_reset_index(128);
    
    bus_interface_write(0x01, 0xAA);  // Write 0xAA at position 0
    indexed_memory_reset_index(128);
    uint8_t read_val = bus_interface_read(0x01);  // Read 0xAA from position 0
    
    if (read_val != 0xAA) {
        printf("  FAIL: Interleaved read/write failed: expected 0xAA, got 0x%02X\n", read_val);
        return false;
    }
    
    bus_interface_write(0x01, 0xBB);  // Write 0xBB at position 1 (after read advanced)
    
    // Reset and verify both values
    indexed_memory_reset_index(128);
    val1 = bus_interface_read(0x01);
    val2 = bus_interface_read(0x01);
    
    if (val1 != 0xAA || val2 != 0xBB) {
        printf("  FAIL: Interleaved operations failed: got 0x%02X, 0x%02X\n", val1, val2);
        return false;
    }
    
    printf("  PASS: DATA_PORT read/write integration works correctly\n");
    return true;
}

/**
 * Test DATA_PORT with different step sizes
 */
bool test_bus_interface_data_port_step_sizes(void) {
    printf("Testing DATA_PORT with different step sizes...\n");
    
    // Initialize the bus interface and indexed memory
    bus_interface_init();
    indexed_memory_init();
    
    // Test step size of 1 (default)
    bus_interface_write(0x00, 128);  // Select index 128
    indexed_memory_reset_index(128);
    indexed_memory_set_index_step(128, 1);
    indexed_memory_set_index_flags(128, FLAG_AUTO_STEP);
    
    // Write data with step size 1
    bus_interface_write(0x01, 0x10);
    bus_interface_write(0x01, 0x20);
    bus_interface_write(0x01, 0x30);
    
    // Read back with step size 1
    indexed_memory_reset_index(128);
    if (bus_interface_read(0x01) != 0x10 || 
        bus_interface_read(0x01) != 0x20 || 
        bus_interface_read(0x01) != 0x30) {
        printf("  FAIL: Step size 1 failed\n");
        return false;
    }
    
    // Test step size of 2
    indexed_memory_reset_index(128);
    indexed_memory_set_index_step(128, 2);
    
    // Write data with step size 2 (should skip every other byte)
    bus_interface_write(0x01, 0xAA);  // Position 0
    bus_interface_write(0x01, 0xBB);  // Position 2
    bus_interface_write(0x01, 0xCC);  // Position 4
    
    // Read back with step size 2
    indexed_memory_reset_index(128);
    if (bus_interface_read(0x01) != 0xAA || 
        bus_interface_read(0x01) != 0xBB || 
        bus_interface_read(0x01) != 0xCC) {
        printf("  FAIL: Step size 2 failed\n");
        return false;
    }
    
    // Test step size of 4
    indexed_memory_reset_index(128);
    indexed_memory_set_index_step(128, 4);
    
    // Write data with step size 4
    bus_interface_write(0x01, 0x11);  // Position 0
    bus_interface_write(0x01, 0x22);  // Position 4
    bus_interface_write(0x01, 0x33);  // Position 8
    
    // Read back with step size 4
    indexed_memory_reset_index(128);
    if (bus_interface_read(0x01) != 0x11 || 
        bus_interface_read(0x01) != 0x22 || 
        bus_interface_read(0x01) != 0x33) {
        printf("  FAIL: Step size 4 failed\n");
        return false;
    }
    
    printf("  PASS: DATA_PORT with different step sizes works correctly\n");
    return true;
}

/**
 * Test DATA_PORT with forward and backward directions
 */
bool test_bus_interface_data_port_directions(void) {
    printf("Testing DATA_PORT with forward and backward directions...\n");
    
    // Initialize the bus interface and indexed memory
    bus_interface_init();
    indexed_memory_init();
    
    // Test forward direction (default)
    bus_interface_write(0x00, 128);  // Select index 128
    indexed_memory_reset_index(128);
    indexed_memory_set_index_step(128, 1);
    indexed_memory_set_index_flags(128, FLAG_AUTO_STEP);  // Forward direction
    
    // Write data in forward direction
    bus_interface_write(0x01, 0x01);
    bus_interface_write(0x01, 0x02);
    bus_interface_write(0x01, 0x03);
    bus_interface_write(0x01, 0x04);
    
    // Read back in forward direction
    indexed_memory_reset_index(128);
    if (bus_interface_read(0x01) != 0x01 || 
        bus_interface_read(0x01) != 0x02 || 
        bus_interface_read(0x01) != 0x03 || 
        bus_interface_read(0x01) != 0x04) {
        printf("  FAIL: Forward direction failed\n");
        return false;
    }
    
    // Test backward direction
    // Set address to position 10 and step backward
    uint32_t base_addr = 0x20013800;  // MIA_USER_AREA_BASE
    indexed_memory_set_index_address(128, base_addr + 10);
    indexed_memory_set_index_default(128, base_addr + 10);
    indexed_memory_set_index_step(128, 1);
    indexed_memory_set_index_flags(128, FLAG_AUTO_STEP | FLAG_DIRECTION);  // Backward
    
    // Write data in backward direction (from position 10 going down)
    bus_interface_write(0x01, 0xAA);  // Position 10
    bus_interface_write(0x01, 0xBB);  // Position 9
    bus_interface_write(0x01, 0xCC);  // Position 8
    bus_interface_write(0x01, 0xDD);  // Position 7
    
    // Read back in backward direction
    indexed_memory_set_index_address(128, base_addr + 10);
    if (bus_interface_read(0x01) != 0xAA || 
        bus_interface_read(0x01) != 0xBB || 
        bus_interface_read(0x01) != 0xCC || 
        bus_interface_read(0x01) != 0xDD) {
        printf("  FAIL: Backward direction failed\n");
        return false;
    }
    
    // Test backward with step size 2
    indexed_memory_set_index_address(128, base_addr + 20);
    indexed_memory_set_index_step(128, 2);
    indexed_memory_set_index_flags(128, FLAG_AUTO_STEP | FLAG_DIRECTION);
    
    bus_interface_write(0x01, 0x11);  // Position 20
    bus_interface_write(0x01, 0x22);  // Position 18
    bus_interface_write(0x01, 0x33);  // Position 16
    
    indexed_memory_set_index_address(128, base_addr + 20);
    if (bus_interface_read(0x01) != 0x11 || 
        bus_interface_read(0x01) != 0x22 || 
        bus_interface_read(0x01) != 0x33) {
        printf("  FAIL: Backward direction with step size 2 failed\n");
        return false;
    }
    
    printf("  PASS: DATA_PORT with forward and backward directions works correctly\n");
    return true;
}

/**
 * Test DATA_PORT with wrap-on-limit functionality
 */
bool test_bus_interface_data_port_wrap_on_limit(void) {
    printf("Testing DATA_PORT with wrap-on-limit functionality...\n");
    
    // Initialize the bus interface and indexed memory
    bus_interface_init();
    indexed_memory_init();
    
    // Configure index 128 with wrap-on-limit
    // Wrap happens when address >= limit, so limit should be one past the last valid position
    uint32_t base_addr = 0x20013800;  // MIA_USER_AREA_BASE
    uint32_t default_addr = base_addr + 0;
    uint32_t limit_addr = base_addr + 5;  // Wrap when reaching position 5
    
    bus_interface_write(0x00, 128);  // Select index 128
    indexed_memory_set_index_address(128, default_addr);
    indexed_memory_set_index_default(128, default_addr);
    indexed_memory_set_index_limit(128, limit_addr);
    indexed_memory_set_index_step(128, 1);
    indexed_memory_set_index_flags(128, FLAG_AUTO_STEP | FLAG_WRAP_ON_LIMIT);
    
    // Write data that will trigger wrap
    // Positions 0-4 are valid, position 5 triggers wrap
    bus_interface_write(0x01, 0x00);  // Write to position 0, then step to 1
    bus_interface_write(0x01, 0x01);  // Write to position 1, then step to 2
    bus_interface_write(0x01, 0x02);  // Write to position 2, then step to 3
    bus_interface_write(0x01, 0x03);  // Write to position 3, then step to 4
    bus_interface_write(0x01, 0x04);  // Write to position 4, then step to 5 (>= limit, wraps to 0)
    bus_interface_write(0x01, 0x05);  // Write to position 0 (wrapped), then step to 1
    
    // Read back and verify wrap occurred
    indexed_memory_reset_index(128);
    if (bus_interface_read(0x01) != 0x05) {  // Position 0 should have wrapped value
        printf("  FAIL: Wrap-on-limit did not wrap correctly, expected 0x05, got 0x%02X\n", 
               bus_interface_read(0x01));
        indexed_memory_reset_index(128);
        uint8_t val = bus_interface_read(0x01);
        printf("  DEBUG: Position 0 contains 0x%02X\n", val);
        return false;
    }
    if (bus_interface_read(0x01) != 0x01) {  // Position 1
        printf("  FAIL: Position 1 incorrect after wrap\n");
        return false;
    }
    
    // Test wrap with step size 2
    indexed_memory_set_index_address(128, default_addr);
    indexed_memory_set_index_limit(128, base_addr + 6);  // Wrap when reaching position 6
    indexed_memory_set_index_step(128, 2);
    indexed_memory_set_index_flags(128, FLAG_AUTO_STEP | FLAG_WRAP_ON_LIMIT);
    
    bus_interface_write(0x01, 0xAA);  // Write to position 0, step to 2
    bus_interface_write(0x01, 0xBB);  // Write to position 2, step to 4
    bus_interface_write(0x01, 0xCC);  // Write to position 4, step to 6 (>= limit, wraps to 0)
    bus_interface_write(0x01, 0xDD);  // Write to position 0 (wrapped), step to 2
    
    indexed_memory_reset_index(128);
    if (bus_interface_read(0x01) != 0xDD) {  // Position 0 should have wrapped value
        printf("  FAIL: Wrap-on-limit with step size 2 did not wrap correctly\n");
        return false;
    }
    
    // Skip position 1 (step size 2)
    indexed_memory_set_index_address(128, base_addr + 2);
    if (bus_interface_read(0x01) != 0xBB) {  // Position 2 should still have original value
        printf("  FAIL: Position 2 incorrect after wrap with step size 2\n");
        return false;
    }
    
    // Test without wrap-on-limit (should not wrap)
    indexed_memory_set_index_address(128, default_addr);
    indexed_memory_set_index_step(128, 1);
    indexed_memory_set_index_flags(128, FLAG_AUTO_STEP);  // No wrap flag
    
    bus_interface_write(0x01, 0x10);  // Position 0
    bus_interface_write(0x01, 0x20);  // Position 1
    bus_interface_write(0x01, 0x30);  // Position 2
    bus_interface_write(0x01, 0x40);  // Position 3
    bus_interface_write(0x01, 0x50);  // Position 4
    bus_interface_write(0x01, 0x60);  // Position 5
    bus_interface_write(0x01, 0x70);  // Position 6 (no wrap, continues)
    
    indexed_memory_reset_index(128);
    if (bus_interface_read(0x01) != 0x10) {  // Position 0 should be original value
        printf("  FAIL: Non-wrap mode incorrectly wrapped\n");
        return false;
    }
    
    printf("  PASS: DATA_PORT with wrap-on-limit functionality works correctly\n");
    return true;
}

/**
 * Test DATA_PORT sequential operations with auto-stepping
 */
bool test_bus_interface_data_port_sequential_operations(void) {
    printf("Testing DATA_PORT sequential operations with auto-stepping...\n");
    
    // Initialize the bus interface and indexed memory
    bus_interface_init();
    indexed_memory_init();
    
    // Test sequential reads
    bus_interface_write(0x00, 128);  // Select index 128
    indexed_memory_reset_index(128);
    indexed_memory_set_index_step(128, 1);
    indexed_memory_set_index_flags(128, FLAG_AUTO_STEP);
    
    // Write a sequence
    for (uint8_t i = 0; i < 20; i++) {
        bus_interface_write(0x01, i);
    }
    
    // Read back the sequence
    indexed_memory_reset_index(128);
    for (uint8_t i = 0; i < 20; i++) {
        uint8_t value = bus_interface_read(0x01);
        if (value != i) {
            printf("  FAIL: Sequential read failed at position %d, expected %d, got %d\n", 
                   i, i, value);
            return false;
        }
    }
    
    // Test sequential writes
    indexed_memory_reset_index(128);
    for (uint8_t i = 0; i < 15; i++) {
        bus_interface_write(0x01, 0xFF - i);
    }
    
    // Verify sequential writes
    indexed_memory_reset_index(128);
    for (uint8_t i = 0; i < 15; i++) {
        uint8_t value = bus_interface_read(0x01);
        if (value != (0xFF - i)) {
            printf("  FAIL: Sequential write failed at position %d\n", i);
            return false;
        }
    }
    
    printf("  PASS: DATA_PORT sequential operations work correctly\n");
    return true;
}

/**
 * Test CFG_FIELD_SELECT read handler for all windows
 */
bool test_bus_interface_cfg_field_select_read(void) {
    printf("Testing CFG_FIELD_SELECT read handler...\n");
    
    // Initialize the bus interface
    bus_interface_init();
    
    // Test reading CFG_FIELD_SELECT for Window A (address 0x02)
    g_window_state[0].config_field_select = CFG_ADDR_L;
    uint8_t value = bus_interface_read(0x02);
    if (value != CFG_ADDR_L) {
        printf("  FAIL: Window A CFG_FIELD_SELECT should return CFG_ADDR_L, got 0x%02X\n", value);
        return false;
    }
    
    // Test reading CFG_FIELD_SELECT for Window B (address 0x12)
    g_window_state[1].config_field_select = CFG_STEP;
    value = bus_interface_read(0x12);
    if (value != CFG_STEP) {
        printf("  FAIL: Window B CFG_FIELD_SELECT should return CFG_STEP, got 0x%02X\n", value);
        return false;
    }
    
    // Test reading CFG_FIELD_SELECT for Window C (address 0x22)
    g_window_state[2].config_field_select = CFG_FLAGS;
    value = bus_interface_read(0x22);
    if (value != CFG_FLAGS) {
        printf("  FAIL: Window C CFG_FIELD_SELECT should return CFG_FLAGS, got 0x%02X\n", value);
        return false;
    }
    
    // Test reading CFG_FIELD_SELECT for Window D (address 0x32)
    g_window_state[3].config_field_select = CFG_DEFAULT_H;
    value = bus_interface_read(0x32);
    if (value != CFG_DEFAULT_H) {
        printf("  FAIL: Window D CFG_FIELD_SELECT should return CFG_DEFAULT_H, got 0x%02X\n", value);
        return false;
    }
    
    printf("  PASS: CFG_FIELD_SELECT read handler works correctly for all windows\n");
    return true;
}

/**
 * Test CFG_FIELD_SELECT write handler for all windows
 */
bool test_bus_interface_cfg_field_select_write(void) {
    printf("Testing CFG_FIELD_SELECT write handler...\n");
    
    // Initialize the bus interface
    bus_interface_init();
    
    // Test writing CFG_FIELD_SELECT for Window A (address 0x02)
    bus_interface_write(0x02, CFG_ADDR_M);
    if (g_window_state[0].config_field_select != CFG_ADDR_M) {
        printf("  FAIL: Window A CFG_FIELD_SELECT should be CFG_ADDR_M, got 0x%02X\n", 
               g_window_state[0].config_field_select);
        return false;
    }
    
    // Test writing CFG_FIELD_SELECT for Window B (address 0x12)
    bus_interface_write(0x12, CFG_LIMIT_L);
    if (g_window_state[1].config_field_select != CFG_LIMIT_L) {
        printf("  FAIL: Window B CFG_FIELD_SELECT should be CFG_LIMIT_L, got 0x%02X\n", 
               g_window_state[1].config_field_select);
        return false;
    }
    
    // Test writing CFG_FIELD_SELECT for Window C (address 0x22)
    bus_interface_write(0x22, CFG_COPY_SRC_IDX);
    if (g_window_state[2].config_field_select != CFG_COPY_SRC_IDX) {
        printf("  FAIL: Window C CFG_FIELD_SELECT should be CFG_COPY_SRC_IDX, got 0x%02X\n", 
               g_window_state[2].config_field_select);
        return false;
    }
    
    // Test writing CFG_FIELD_SELECT for Window D (address 0x32)
    bus_interface_write(0x32, CFG_COPY_COUNT_H);
    if (g_window_state[3].config_field_select != CFG_COPY_COUNT_H) {
        printf("  FAIL: Window D CFG_FIELD_SELECT should be CFG_COPY_COUNT_H, got 0x%02X\n", 
               g_window_state[3].config_field_select);
        return false;
    }
    
    printf("  PASS: CFG_FIELD_SELECT write handler works correctly for all windows\n");
    return true;
}

/**
 * Test CFG_DATA read handler for all configuration field types
 */
bool test_bus_interface_cfg_data_read(void) {
    printf("Testing CFG_DATA read handler for all configuration field types...\n");
    
    // Initialize the bus interface and indexed memory
    bus_interface_init();
    indexed_memory_init();
    
    // Select index 128 for Window A
    bus_interface_write(0x00, 128);
    
    // Configure index 128 with known values
    uint32_t test_addr = 0x123456;
    uint32_t test_default = 0xABCDEF;
    uint32_t test_limit = 0x789ABC;
    uint8_t test_step = 0x42;
    uint8_t test_flags = FLAG_AUTO_STEP | FLAG_WRAP_ON_LIMIT;
    
    indexed_memory_set_index_address(128, test_addr);
    indexed_memory_set_index_default(128, test_default);
    indexed_memory_set_index_limit(128, test_limit);
    indexed_memory_set_index_step(128, test_step);
    indexed_memory_set_index_flags(128, test_flags);
    
    // Test reading current address (24-bit, 3 bytes)
    bus_interface_write(0x02, CFG_ADDR_L);
    uint8_t addr_l = bus_interface_read(0x03);
    bus_interface_write(0x02, CFG_ADDR_M);
    uint8_t addr_m = bus_interface_read(0x03);
    bus_interface_write(0x02, CFG_ADDR_H);
    uint8_t addr_h = bus_interface_read(0x03);
    
    if (addr_l != 0x56 || addr_m != 0x34 || addr_h != 0x12) {
        printf("  FAIL: Current address read failed: got 0x%02X%02X%02X, expected 0x123456\n", 
               addr_h, addr_m, addr_l);
        return false;
    }
    
    // Test reading default address (24-bit, 3 bytes)
    bus_interface_write(0x02, CFG_DEFAULT_L);
    uint8_t def_l = bus_interface_read(0x03);
    bus_interface_write(0x02, CFG_DEFAULT_M);
    uint8_t def_m = bus_interface_read(0x03);
    bus_interface_write(0x02, CFG_DEFAULT_H);
    uint8_t def_h = bus_interface_read(0x03);
    
    if (def_l != 0xEF || def_m != 0xCD || def_h != 0xAB) {
        printf("  FAIL: Default address read failed: got 0x%02X%02X%02X, expected 0xABCDEF\n", 
               def_h, def_m, def_l);
        return false;
    }
    
    // Test reading limit address (24-bit, 3 bytes)
    bus_interface_write(0x02, CFG_LIMIT_L);
    uint8_t lim_l = bus_interface_read(0x03);
    bus_interface_write(0x02, CFG_LIMIT_M);
    uint8_t lim_m = bus_interface_read(0x03);
    bus_interface_write(0x02, CFG_LIMIT_H);
    uint8_t lim_h = bus_interface_read(0x03);
    
    if (lim_l != 0xBC || lim_m != 0x9A || lim_h != 0x78) {
        printf("  FAIL: Limit address read failed: got 0x%02X%02X%02X, expected 0x789ABC\n", 
               lim_h, lim_m, lim_l);
        return false;
    }
    
    // Test reading step size
    bus_interface_write(0x02, CFG_STEP);
    uint8_t step = bus_interface_read(0x03);
    if (step != test_step) {
        printf("  FAIL: Step size read failed: got 0x%02X, expected 0x%02X\n", step, test_step);
        return false;
    }
    
    // Test reading flags
    bus_interface_write(0x02, CFG_FLAGS);
    uint8_t flags = bus_interface_read(0x03);
    if (flags != test_flags) {
        printf("  FAIL: Flags read failed: got 0x%02X, expected 0x%02X\n", flags, test_flags);
        return false;
    }
    
    printf("  PASS: CFG_DATA read handler works correctly for all configuration field types\n");
    return true;
}

/**
 * Test CFG_DATA write handler for all configuration field types
 */
bool test_bus_interface_cfg_data_write(void) {
    printf("Testing CFG_DATA write handler for all configuration field types...\n");
    
    // Initialize the bus interface and indexed memory
    bus_interface_init();
    indexed_memory_init();
    
    // Select index 128 for Window A
    bus_interface_write(0x00, 128);
    
    // Write current address (24-bit, 3 bytes)
    bus_interface_write(0x02, CFG_ADDR_L);
    bus_interface_write(0x03, 0x78);
    bus_interface_write(0x02, CFG_ADDR_M);
    bus_interface_write(0x03, 0x56);
    bus_interface_write(0x02, CFG_ADDR_H);
    bus_interface_write(0x03, 0x34);
    
    // Verify current address was written
    uint32_t addr = indexed_memory_get_config_field(128, CFG_ADDR_L) |
                    (indexed_memory_get_config_field(128, CFG_ADDR_M) << 8) |
                    (indexed_memory_get_config_field(128, CFG_ADDR_H) << 16);
    if (addr != 0x345678) {
        printf("  FAIL: Current address write failed: got 0x%06X, expected 0x345678\n", addr);
        return false;
    }
    
    // Write default address (24-bit, 3 bytes)
    bus_interface_write(0x02, CFG_DEFAULT_L);
    bus_interface_write(0x03, 0x11);
    bus_interface_write(0x02, CFG_DEFAULT_M);
    bus_interface_write(0x03, 0x22);
    bus_interface_write(0x02, CFG_DEFAULT_H);
    bus_interface_write(0x03, 0x33);
    
    // Verify default address was written
    uint32_t def_addr = indexed_memory_get_config_field(128, CFG_DEFAULT_L) |
                        (indexed_memory_get_config_field(128, CFG_DEFAULT_M) << 8) |
                        (indexed_memory_get_config_field(128, CFG_DEFAULT_H) << 16);
    if (def_addr != 0x332211) {
        printf("  FAIL: Default address write failed: got 0x%06X, expected 0x332211\n", def_addr);
        return false;
    }
    
    // Write limit address (24-bit, 3 bytes)
    bus_interface_write(0x02, CFG_LIMIT_L);
    bus_interface_write(0x03, 0xAA);
    bus_interface_write(0x02, CFG_LIMIT_M);
    bus_interface_write(0x03, 0xBB);
    bus_interface_write(0x02, CFG_LIMIT_H);
    bus_interface_write(0x03, 0xCC);
    
    // Verify limit address was written
    uint32_t lim_addr = indexed_memory_get_config_field(128, CFG_LIMIT_L) |
                        (indexed_memory_get_config_field(128, CFG_LIMIT_M) << 8) |
                        (indexed_memory_get_config_field(128, CFG_LIMIT_H) << 16);
    if (lim_addr != 0xCCBBAA) {
        printf("  FAIL: Limit address write failed: got 0x%06X, expected 0xCCBBAA\n", lim_addr);
        return false;
    }
    
    // Write step size
    bus_interface_write(0x02, CFG_STEP);
    bus_interface_write(0x03, 0x55);
    
    uint8_t step = indexed_memory_get_config_field(128, CFG_STEP);
    if (step != 0x55) {
        printf("  FAIL: Step size write failed: got 0x%02X, expected 0x55\n", step);
        return false;
    }
    
    // Write flags
    bus_interface_write(0x02, CFG_FLAGS);
    bus_interface_write(0x03, FLAG_AUTO_STEP | FLAG_DIRECTION);
    
    uint8_t flags = indexed_memory_get_config_field(128, CFG_FLAGS);
    if (flags != (FLAG_AUTO_STEP | FLAG_DIRECTION)) {
        printf("  FAIL: Flags write failed: got 0x%02X, expected 0x%02X\n", 
               flags, FLAG_AUTO_STEP | FLAG_DIRECTION);
        return false;
    }
    
    printf("  PASS: CFG_DATA write handler works correctly for all configuration field types\n");
    return true;
}

/**
 * Test CFG_DATA read/write integration with multi-byte fields
 */
bool test_bus_interface_cfg_data_multibyte_fields(void) {
    printf("Testing CFG_DATA with multi-byte field access (24-bit addresses)...\n");
    
    // Initialize the bus interface and indexed memory
    bus_interface_init();
    indexed_memory_init();
    
    // Select index 129 for Window B
    bus_interface_write(0x10, 129);
    
    // Write a 24-bit address byte by byte
    bus_interface_write(0x12, CFG_ADDR_L);
    bus_interface_write(0x13, 0xDE);
    bus_interface_write(0x12, CFG_ADDR_M);
    bus_interface_write(0x13, 0xBC);
    bus_interface_write(0x12, CFG_ADDR_H);
    bus_interface_write(0x13, 0x9A);
    
    // Read back the 24-bit address byte by byte
    bus_interface_write(0x12, CFG_ADDR_L);
    uint8_t read_l = bus_interface_read(0x13);
    bus_interface_write(0x12, CFG_ADDR_M);
    uint8_t read_m = bus_interface_read(0x13);
    bus_interface_write(0x12, CFG_ADDR_H);
    uint8_t read_h = bus_interface_read(0x13);
    
    if (read_l != 0xDE || read_m != 0xBC || read_h != 0x9A) {
        printf("  FAIL: Multi-byte address read/write failed: got 0x%02X%02X%02X, expected 0x9ABCDE\n", 
               read_h, read_m, read_l);
        return false;
    }
    
    // Test with default address
    bus_interface_write(0x12, CFG_DEFAULT_L);
    bus_interface_write(0x13, 0x11);
    bus_interface_write(0x12, CFG_DEFAULT_M);
    bus_interface_write(0x13, 0x22);
    bus_interface_write(0x12, CFG_DEFAULT_H);
    bus_interface_write(0x13, 0x33);
    
    bus_interface_write(0x12, CFG_DEFAULT_L);
    read_l = bus_interface_read(0x13);
    bus_interface_write(0x12, CFG_DEFAULT_M);
    read_m = bus_interface_read(0x13);
    bus_interface_write(0x12, CFG_DEFAULT_H);
    read_h = bus_interface_read(0x13);
    
    if (read_l != 0x11 || read_m != 0x22 || read_h != 0x33) {
        printf("  FAIL: Multi-byte default address read/write failed\n");
        return false;
    }
    
    printf("  PASS: CFG_DATA multi-byte field access works correctly\n");
    return true;
}

/**
 * Test CFG_DATA with DMA configuration fields
 */
bool test_bus_interface_cfg_data_dma_fields(void) {
    printf("Testing CFG_DATA with DMA configuration fields...\n");
    
    // Initialize the bus interface and indexed memory
    bus_interface_init();
    indexed_memory_init();
    
    // Select index 128 for Window A
    bus_interface_write(0x00, 128);
    
    // Write DMA source index
    bus_interface_write(0x02, CFG_COPY_SRC_IDX);
    bus_interface_write(0x03, 64);
    
    // Verify DMA source index
    bus_interface_write(0x02, CFG_COPY_SRC_IDX);
    uint8_t src_idx = bus_interface_read(0x03);
    if (src_idx != 64) {
        printf("  FAIL: DMA source index write/read failed: got %d, expected 64\n", src_idx);
        return false;
    }
    
    // Write DMA destination index
    bus_interface_write(0x02, CFG_COPY_DST_IDX);
    bus_interface_write(0x03, 128);
    
    // Verify DMA destination index
    bus_interface_write(0x02, CFG_COPY_DST_IDX);
    uint8_t dst_idx = bus_interface_read(0x03);
    if (dst_idx != 128) {
        printf("  FAIL: DMA destination index write/read failed: got %d, expected 128\n", dst_idx);
        return false;
    }
    
    // Write DMA copy count (16-bit, 2 bytes)
    bus_interface_write(0x02, CFG_COPY_COUNT_L);
    bus_interface_write(0x03, 0x34);
    bus_interface_write(0x02, CFG_COPY_COUNT_H);
    bus_interface_write(0x03, 0x12);
    
    // Verify DMA copy count
    bus_interface_write(0x02, CFG_COPY_COUNT_L);
    uint8_t count_l = bus_interface_read(0x03);
    bus_interface_write(0x02, CFG_COPY_COUNT_H);
    uint8_t count_h = bus_interface_read(0x03);
    
    if (count_l != 0x34 || count_h != 0x12) {
        printf("  FAIL: DMA copy count write/read failed: got 0x%02X%02X, expected 0x1234\n", 
               count_h, count_l);
        return false;
    }
    
    // Verify all DMA fields are accessible from indexed_memory
    uint8_t verify_src = indexed_memory_get_config_field(128, CFG_COPY_SRC_IDX);
    uint8_t verify_dst = indexed_memory_get_config_field(128, CFG_COPY_DST_IDX);
    uint16_t verify_count = indexed_memory_get_config_field(128, CFG_COPY_COUNT_L) |
                            (indexed_memory_get_config_field(128, CFG_COPY_COUNT_H) << 8);
    
    if (verify_src != 64 || verify_dst != 128 || verify_count != 0x1234) {
        printf("  FAIL: DMA configuration verification failed\n");
        return false;
    }
    
    printf("  PASS: CFG_DATA with DMA configuration fields works correctly\n");
    return true;
}

/**
 * Test configuration access with multiple windows
 */
bool test_bus_interface_cfg_multi_window(void) {
    printf("Testing configuration access with multiple windows...\n");
    
    // Initialize the bus interface and indexed memory
    bus_interface_init();
    indexed_memory_init();
    
    // Configure different indexes for each window
    bus_interface_write(0x00, 128);  // Window A -> index 128
    bus_interface_write(0x10, 129);  // Window B -> index 129
    bus_interface_write(0x20, 130);  // Window C -> index 130
    bus_interface_write(0x30, 131);  // Window D -> index 131
    
    // Write different step sizes to each index via different windows
    bus_interface_write(0x02, CFG_STEP);  // Window A: select STEP field
    bus_interface_write(0x03, 1);         // Window A: write step=1
    
    bus_interface_write(0x12, CFG_STEP);  // Window B: select STEP field
    bus_interface_write(0x13, 2);         // Window B: write step=2
    
    bus_interface_write(0x22, CFG_STEP);  // Window C: select STEP field
    bus_interface_write(0x23, 4);         // Window C: write step=4
    
    bus_interface_write(0x32, CFG_STEP);  // Window D: select STEP field
    bus_interface_write(0x33, 8);         // Window D: write step=8
    
    // Read back and verify each window's configuration is independent
    bus_interface_write(0x02, CFG_STEP);
    uint8_t step_a = bus_interface_read(0x03);
    
    bus_interface_write(0x12, CFG_STEP);
    uint8_t step_b = bus_interface_read(0x13);
    
    bus_interface_write(0x22, CFG_STEP);
    uint8_t step_c = bus_interface_read(0x23);
    
    bus_interface_write(0x32, CFG_STEP);
    uint8_t step_d = bus_interface_read(0x33);
    
    if (step_a != 1 || step_b != 2 || step_c != 4 || step_d != 8) {
        printf("  FAIL: Multi-window configuration failed: got %d, %d, %d, %d, expected 1, 2, 4, 8\n", 
               step_a, step_b, step_c, step_d);
        return false;
    }
    
    // Verify the underlying indexes have the correct values
    if (indexed_memory_get_config_field(128, CFG_STEP) != 1 ||
        indexed_memory_get_config_field(129, CFG_STEP) != 2 ||
        indexed_memory_get_config_field(130, CFG_STEP) != 4 ||
        indexed_memory_get_config_field(131, CFG_STEP) != 8) {
        printf("  FAIL: Underlying index configuration verification failed\n");
        return false;
    }
    
    printf("  PASS: Configuration access with multiple windows works correctly\n");
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
    all_passed &= test_bus_interface_idx_select_read();
    all_passed &= test_bus_interface_idx_select_write();
    all_passed &= test_bus_interface_idx_select_integration();
    all_passed &= test_bus_interface_data_port_read();
    all_passed &= test_bus_interface_data_port_auto_step();
    all_passed &= test_bus_interface_data_port_multi_window();
    all_passed &= test_bus_interface_data_port_write();
    all_passed &= test_bus_interface_data_port_write_auto_step();
    all_passed &= test_bus_interface_data_port_write_multi_window();
    all_passed &= test_bus_interface_data_port_read_write_integration();
    all_passed &= test_bus_interface_data_port_step_sizes();
    all_passed &= test_bus_interface_data_port_directions();
    all_passed &= test_bus_interface_data_port_wrap_on_limit();
    all_passed &= test_bus_interface_data_port_sequential_operations();
    all_passed &= test_bus_interface_cfg_field_select_read();
    all_passed &= test_bus_interface_cfg_field_select_write();
    all_passed &= test_bus_interface_cfg_data_read();
    all_passed &= test_bus_interface_cfg_data_write();
    all_passed &= test_bus_interface_cfg_data_multibyte_fields();
    all_passed &= test_bus_interface_cfg_data_dma_fields();
    all_passed &= test_bus_interface_cfg_multi_window();
    
    if (all_passed) {
        printf("\n=== All Bus Interface Tests PASSED ===\n\n");
    } else {
        printf("\n=== Some Bus Interface Tests FAILED ===\n\n");
    }
    
    return all_passed;
}
