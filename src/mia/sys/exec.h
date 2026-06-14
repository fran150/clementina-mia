#ifndef _MIA_SYS_EXEC_H_
#define _MIA_SYS_EXEC_H_

#include <stdbool.h>

void mia_exec_pause(void);
void mia_exec_resume(void);
bool mia_exec_is_paused(void);
void mia_exec_print_status(void);

#endif
