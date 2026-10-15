#ifndef _MIA_CMDS_H_
#define _MIA_CMDS_H_

#include <stdint.h>

// Core command ids: indexes, DMA copies and execution control. Each subsystem's
// own command ids live in its header (video.h, input.h, timing.h, audio.h, sd.h).
#define MIA_CMD_RESET_INDEX_A 0x00u
#define MIA_CMD_RESET_INDEX_B 0x01u
#define MIA_CMD_RESET_INDEX 0x02u
#define MIA_CMD_CURRENT_TO_DEFAULT 0x03u
#define MIA_CMD_CURRENT_TO_LIMIT 0x04u
#define MIA_CMD_RESET_ALL_INDEXES 0x05u
#define MIA_CMD_PEEK_INDEX_TO_A 0x06u
#define MIA_CMD_PEEK_INDEX_TO_B 0x07u

#define MIA_CMD_COPY_INDEXES 0x10u
#define MIA_CMD_COPY_RECT 0x11u

#define MIA_CMD_EXEC_PAUSE 0x30u

// Inits the command system. This prepares the lookup table of commands
// and sets up the code for multicore communication
void mia_command_init();

#endif