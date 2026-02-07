#ifndef _MIA_CMDS_H_
#define _MIA_CMDS_H_

#include <stdint.h>

// Inits the command system. This prepares the lookup table of commands
// and sets up the code for multicore communication
void mia_command_init();

#endif