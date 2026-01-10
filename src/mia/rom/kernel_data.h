/**
 * Kernel Data Interface
 * External declarations for kernel binary data loaded from kernel.bin
 */

#ifndef KERNEL_DATA_H
#define KERNEL_DATA_H

#include <stdint.h>
#include <stddef.h>

// External kernel data generated from kernel.bin at build time
extern const uint8_t kernel_data[];
extern const size_t kernel_data_size;

#endif // KERNEL_DATA_H