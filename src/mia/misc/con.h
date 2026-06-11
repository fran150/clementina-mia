#ifndef _MIA_MISC_CON_H_
#define _MIA_MISC_CON_H_

// Non-blocking console poll — call from the main loop each iteration.
// Accumulates typed characters, handles backspace, and dispatches the
// appropriate command handler (normal or monitor) when Enter is pressed.
void con_process(void);

#endif
