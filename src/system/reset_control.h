/**
 * Reset Control System for MIA
 * Manages reset line control for Clementina system
 */

#ifndef RESET_CONTROL_H
#define RESET_CONTROL_H

#include <stdint.h>
#include <stdbool.h>

// Reset timing constants
#define RESET_ASSERT_TIME_MS    10   // Minimum reset assertion time

// Function prototypes
void reset_control_init(void);
void reset_control_assert_reset(void);
void reset_control_release_reset(void);
bool reset_control_is_reset_asserted(void);
void reset_control_process(void);
void reset_control_software_reset(void);

#endif // RESET_CONTROL_H