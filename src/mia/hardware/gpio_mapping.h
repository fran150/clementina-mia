#ifndef _MIA_HARDWARE_GPIO_MAPPING_H_
#define _MIA_HARDWARE_GPIO_MAPPING_H_

// These are pins that the MIA controls

// Reset pin
#define CPU_RESB_PIN 26
// IRQ pin     
#define CPU_IRQB_PIN 22
// // PHI2 (clock) pin
#define CPU_PHI2_PIN 21         

// Usually these pins are handled in a group

// Pin mapping start
#define MIA_PIN_BASE 6                              
// CS pin
#define MIA_CS_PIN (MIA_PIN_BASE + 0)               
// R/W pin
#define MIA_RWB_PIN (MIA_PIN_BASE + 1)              
// Data bus base pin (8 pins)
#define MIA_DATA_PIN_BASE (MIA_PIN_BASE + 2)
// Address bus base pin (5 pins)
#define MIA_ADDR_PIN_BASE (MIA_PIN_BASE + 10)

#endif