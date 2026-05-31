#ifndef _MIA_HARDWARE_PIO_MAPPING_H_
#define _MIA_HARDWARE_PIO_MAPPING_H_

// PIO used for CS and R/W signal detection and data pin direction handling program
#define MIA_CS_RWB_PIO pio0
// SM used for CS and R/W signal detection and data pin direction handling program
#define MIA_CS_RWB_SM 0

// PIO used for writes from 6502
#define MIA_WRITE_PIO pio0
// SM used for writes from 6502
#define MIA_WRITE_SM 1

// PIO used for reads from 6502
#define MIA_READ_PIO pio0
// SM used for reads from 6502
#define MIA_READ_SM 2

// PIO used to process actions after read / write from 6502
#define MIA_ACT_PIO pio1
// SM used to process actions after read / write from 6502
#define MIA_ACT_SM 0

#endif
