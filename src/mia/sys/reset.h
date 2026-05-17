#ifndef _MIA_SYS_RESET_H_
#define _MIA_SYS_RESET_H_

void mia_prepare_reset_lines(void);
void mia_handle_reset_request(void);
void mia_pulse_cpu_reset(void);

#endif
