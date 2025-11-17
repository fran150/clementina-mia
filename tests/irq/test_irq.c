/**
 * MIA IRQ System Tests
 * 
 * Tests for the centralized interrupt management system
 */

#include "test_irq.h"
#include "irq/irq.h"
#include "mocks/pico_mock.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>

/**
 * Test IRQ system initialization
 */
static void test_irq_initialization(void) {
    printf("Testing IRQ system initialization...\n");
    
    // Initialize IRQ system
    irq_init();
    
    // Verify initial state
    if (irq_get_cause() != IRQ_NO_IRQ) {
        printf("  FAIL: Initial IRQ cause should be IRQ_NO_IRQ, got 0x%04X\n", irq_get_cause());
        return;
    }
    
    if (irq_get_mask() != 0xFFFF) {
        printf("  FAIL: Initial IRQ mask should be 0xFFFF, got 0x%04X\n", irq_get_mask());
        return;
    }
    
    if (irq_get_enable() != 0x01) {
        printf("  FAIL: Initial IRQ enable should be 0x01, got 0x%02X\n", irq_get_enable());
        return;
    }
    
    if (irq_is_pending()) {
        printf("  FAIL: No IRQ should be pending initially\n");
        return;
    }
    
    printf("  PASS: IRQ system initialization\n");
}

/**
 * Test IRQ cause setting and clearing
 */
static void test_irq_cause_management(void) {
    printf("Testing IRQ cause management...\n");
    
    irq_init();
    
    // Test setting single IRQ
    irq_set(IRQ_DMA_COMPLETE);
    if (irq_get_cause() != IRQ_DMA_COMPLETE) {
        printf("  FAIL: IRQ cause should be 0x%04X, got 0x%04X\n", IRQ_DMA_COMPLETE, irq_get_cause());
        return;
    }
    
    // Test accumulating IRQs
    irq_set(IRQ_MEMORY_ERROR);
    uint16_t expected = IRQ_DMA_COMPLETE | IRQ_MEMORY_ERROR;
    if (irq_get_cause() != expected) {
        printf("  FAIL: IRQ cause should be 0x%04X, got 0x%04X\n", expected, irq_get_cause());
        return;
    }
    
    // Test clearing specific IRQ
    mia_irq_clear(IRQ_DMA_COMPLETE);
    if (irq_get_cause() != IRQ_MEMORY_ERROR) {
        printf("  FAIL: IRQ cause should be 0x%04X, got 0x%04X\n", IRQ_MEMORY_ERROR, irq_get_cause());
        return;
    }
    
    // Test clearing all IRQs
    irq_clear_all();
    if (irq_get_cause() != IRQ_NO_IRQ) {
        printf("  FAIL: IRQ cause should be IRQ_NO_IRQ, got 0x%04X\n", irq_get_cause());
        return;
    }
    
    printf("  PASS: IRQ cause management\n");
}

/**
 * Test IRQ cause low/high byte access
 */
static void test_irq_cause_byte_access(void) {
    printf("Testing IRQ cause byte access...\n");
    
    irq_init();
    
    // Set low byte IRQ
    irq_set(IRQ_DMA_COMPLETE);
    if (irq_get_cause_low() != (IRQ_DMA_COMPLETE & 0xFF)) {
        printf("  FAIL: IRQ cause low should be 0x%02X, got 0x%02X\n", 
               (IRQ_DMA_COMPLETE & 0xFF), irq_get_cause_low());
        return;
    }
    if (irq_get_cause_high() != 0) {
        printf("  FAIL: IRQ cause high should be 0x00, got 0x%02X\n", irq_get_cause_high());
        return;
    }
    
    // Set high byte IRQ
    irq_set(IRQ_VIDEO_FRAME_COMPLETE);
    if (irq_get_cause_high() != ((IRQ_VIDEO_FRAME_COMPLETE >> 8) & 0xFF)) {
        printf("  FAIL: IRQ cause high should be 0x%02X, got 0x%02X\n", 
               ((IRQ_VIDEO_FRAME_COMPLETE >> 8) & 0xFF), irq_get_cause_high());
        return;
    }
    
    // Test write-1-to-clear low byte
    irq_write_cause_low(IRQ_DMA_COMPLETE & 0xFF);
    if (irq_get_cause_low() != 0) {
        printf("  FAIL: IRQ cause low should be 0x00 after clear, got 0x%02X\n", irq_get_cause_low());
        return;
    }
    if (irq_get_cause_high() == 0) {
        printf("  FAIL: IRQ cause high should still be set\n");
        return;
    }
    
    // Test write-1-to-clear high byte
    irq_write_cause_high((IRQ_VIDEO_FRAME_COMPLETE >> 8) & 0xFF);
    if (irq_get_cause() != IRQ_NO_IRQ) {
        printf("  FAIL: All IRQ causes should be cleared, got 0x%04X\n", irq_get_cause());
        return;
    }
    
    printf("  PASS: IRQ cause byte access\n");
}

/**
 * Test IRQ mask functionality
 */
static void test_irq_mask_functionality(void) {
    printf("Testing IRQ mask functionality...\n");
    
    irq_init();
    
    // Test setting mask
    irq_set_mask(0x00FF);
    if (irq_get_mask() != 0x00FF) {
        printf("  FAIL: IRQ mask should be 0x00FF, got 0x%04X\n", irq_get_mask());
        return;
    }
    
    // Test that masked IRQs don't trigger pending state
    irq_set(IRQ_VIDEO_FRAME_COMPLETE); // High byte, should be masked
    if (irq_is_pending()) {
        printf("  FAIL: Masked IRQ should not be pending\n");
        return;
    }
    
    // Test that unmasked IRQs do trigger pending state
    irq_set(IRQ_DMA_COMPLETE); // Low byte, should not be masked
    if (!irq_is_pending()) {
        printf("  FAIL: Unmasked IRQ should be pending\n");
        return;
    }
    
    // Test changing mask affects pending state
    irq_set_mask(0xFF00); // Now mask low byte, unmask high byte
    if (irq_is_pending()) {
        printf("  FAIL: IRQ should not be pending after mask change\n");
        return;
    }
    
    printf("  PASS: IRQ mask functionality\n");
}

/**
 * Test IRQ enable/disable functionality
 */
static void test_irq_enable_functionality(void) {
    printf("Testing IRQ enable functionality...\n");
    
    irq_init();
    
    // Set up IRQ condition
    irq_set(IRQ_DMA_COMPLETE);
    if (!irq_is_pending()) {
        printf("  FAIL: IRQ should be pending initially\n");
        return;
    }
    
    // Test disabling IRQs
    irq_set_enable(0);
    if (irq_get_enable() != 0) {
        printf("  FAIL: IRQ enable should be 0, got 0x%02X\n", irq_get_enable());
        return;
    }
    if (irq_is_pending()) {
        printf("  FAIL: IRQ should not be pending when disabled\n");
        return;
    }
    
    // Test re-enabling IRQs
    irq_set_enable(1);
    if (irq_get_enable() != 1) {
        printf("  FAIL: IRQ enable should be 1, got 0x%02X\n", irq_get_enable());
        return;
    }
    if (!irq_is_pending()) {
        printf("  FAIL: IRQ should be pending when re-enabled\n");
        return;
    }
    
    printf("  PASS: IRQ enable functionality\n");
}

/**
 * Test IRQ pending state logic
 */
static void test_irq_pending_logic(void) {
    printf("Testing IRQ pending logic...\n");
    
    irq_init();
    
    // Test: No IRQ pending initially
    if (irq_is_pending()) {
        printf("  FAIL: No IRQ should be pending initially\n");
        return;
    }
    
    // Test: IRQ pending when cause & mask & enable
    irq_set(IRQ_DMA_COMPLETE);
    irq_set_mask(0xFFFF);
    irq_set_enable(1);
    if (!irq_is_pending()) {
        printf("  FAIL: IRQ should be pending (cause & mask & enable)\n");
        return;
    }
    
    // Test: No IRQ pending when masked
    irq_set_mask(0x0000);
    if (irq_is_pending()) {
        printf("  FAIL: IRQ should not be pending when masked\n");
        return;
    }
    
    // Test: No IRQ pending when disabled
    irq_set_mask(0xFFFF);
    irq_set_enable(0);
    if (irq_is_pending()) {
        printf("  FAIL: IRQ should not be pending when disabled\n");
        return;
    }
    
    // Test: No IRQ pending when no cause
    irq_set_enable(1);
    irq_clear_all();
    if (irq_is_pending()) {
        printf("  FAIL: IRQ should not be pending when no cause\n");
        return;
    }
    
    printf("  PASS: IRQ pending logic\n");
}

/**
 * Test multiple IRQ sources
 */
static void test_multiple_irq_sources(void) {
    printf("Testing multiple IRQ sources...\n");
    
    irq_init();
    
    // Set multiple IRQs
    uint16_t irqs = IRQ_MEMORY_ERROR | IRQ_DMA_COMPLETE | IRQ_VIDEO_FRAME_COMPLETE | IRQ_USB_KEYBOARD;
    irq_set(irqs);
    
    if (irq_get_cause() != irqs) {
        printf("  FAIL: IRQ cause should be 0x%04X, got 0x%04X\n", irqs, irq_get_cause());
        return;
    }
    
    // Clear some IRQs
    mia_irq_clear(IRQ_DMA_COMPLETE | IRQ_USB_KEYBOARD);
    uint16_t remaining = IRQ_MEMORY_ERROR | IRQ_VIDEO_FRAME_COMPLETE;
    if (irq_get_cause() != remaining) {
        printf("  FAIL: IRQ cause should be 0x%04X, got 0x%04X\n", remaining, irq_get_cause());
        return;
    }
    
    // Test partial masking
    irq_set_mask(IRQ_MEMORY_ERROR); // Only allow memory error
    if (!irq_is_pending()) {
        printf("  FAIL: Memory error IRQ should be pending\n");
        return;
    }
    
    // Clear the allowed IRQ
    mia_irq_clear(IRQ_MEMORY_ERROR);
    if (irq_is_pending()) {
        printf("  FAIL: No IRQ should be pending after clearing allowed IRQ\n");
        return;
    }
    
    printf("  PASS: Multiple IRQ sources\n");
}

/**
 * Run all IRQ tests
 */
void run_irq_tests(void) {
    printf("\n=== MIA IRQ System Tests ===\n");
    
    test_irq_initialization();
    test_irq_cause_management();
    test_irq_cause_byte_access();
    test_irq_mask_functionality();
    test_irq_enable_functionality();
    test_irq_pending_logic();
    test_multiple_irq_sources();
    
    printf("\n=== Test Results ===\n");
    printf("ALL TESTS PASSED\n");
}
