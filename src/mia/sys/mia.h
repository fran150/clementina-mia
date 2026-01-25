#ifndef _MIA_H_
#define _MIA_H_

#include <stdbool.h>

#define MIA_REG_IDXA_SEL    0x00
#define MIA_REG_IDXA_PORT   0x01
#define MIA_REG_IDXB_SEL    0x02
#define MIA_REG_IDXB_PORT   0x03

#define MIA_REG_CFG_SEL     0x04
#define MIA_REG_CFG_PORT    0x05

#define MIA_CMD_PARAM1      0x06
#define MIA_CMD_PARAM2      0x07
#define MIA_CMD_PARAM3      0x08
#define MIA_CMD_TRIGGER     0x09

#define MIA_ERROR_LSB       0x0A
#define MIA_ERROR_MSB       0x0B

#define MIA_STATUS_LSB      0x0C
#define MIA_STATUS_MSB      0x0D

#define MIA_IRQ_MASK_LSB    0x0E
#define MIA_IRQ_MASK_MSB    0x0F
#define MIA_IRQ_LSB         0x10
#define MIA_IRQ_MSB         0x11

void mia_init(void);

#endif
