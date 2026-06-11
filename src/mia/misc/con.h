#ifndef _MIA_MISC_CON_H_
#define _MIA_MISC_CON_H_

// Non-blocking console poll — call from the main loop each iteration.
// Accumulates typed characters; dispatches a command when Enter is pressed.
void con_process(void);

// Blocking line read with echo and backspace support.
// Reads until Enter, stores result (null-terminated) in buf.
// Used by the monitor loop.
void con_read_line(char *buf, int max_len);

#endif
