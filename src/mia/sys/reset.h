#ifndef _MIA_SYS_RESET_H_
#define _MIA_SYS_RESET_H_

#include <stdbool.h>

void mia_prepare_reset_lines(void);
void mia_handle_reset_request(void);
void mia_set_cpu_reset(bool asserted);
void mia_schedule_cpu_reset_release(void);
void mia_pulse_cpu_reset(void);

#endif
