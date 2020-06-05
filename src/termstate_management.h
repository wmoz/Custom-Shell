#ifndef __TERMSTATE_MANAGEMENT_H
#define __TERMSTATE_MANAGEMENT_H

#include <sys/types.h>

/* Initialize tty support. */
void termstate_init(void);

/* Save current terminal settings.
 * This function should be called when a job is suspended.*/
void termstate_save(struct termios *saved_tty_state);

/**
 * Assign ownership of the terminal to process group
 * pgrp, restoring its terminal state if provided.
 */
void termstate_give_terminal_to(struct termios *pg_tty_state, pid_t pgrp);

/*
 * Restore the shell's terminal state and assign ownership
 * of the terminal to the shell.  The shell should call this
 * before printing a new prompt.
 */
void termstate_give_terminal_back_to_shell(void);

#endif /* __TERMSTATE_MANAGEMENT_H */
