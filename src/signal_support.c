/*
 * Signal-related utility functions
 *
 * Refactored by Godmar Back for CS 3214 Summer 2020
 * Virginia Tech.
 */

#include <signal.h>
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#include "signal_support.h"
#include "utils.h"

/* Return true if this signal is blocked */
bool 
signal_is_blocked(int sig)
{
    sigset_t mask;
    if (sigprocmask(0, NULL, &mask) == -1)
        utils_error("sigprocmask failed while retrieving current mask");

    return sigismember(&mask, sig);
}

/* Helper for signal_block and signal_unblock */
static bool
__mask_signal(int sig, int how)
{
    sigset_t mask, omask;
    sigemptyset(&mask);
    sigaddset(&mask, sig);
    if (sigprocmask(how, &mask, &omask) != 0)
        utils_error("sigprocmask failed for %d/%d", sig, how);
    return sigismember(&omask, sig);
}

/* Block a signal. Returns true it was blocked before */
bool 
signal_block(int sig)
{
    return __mask_signal(sig, SIG_BLOCK);
}

/* Unblock a signal. Returns true it was blocked before */
bool 
signal_unblock(int sig)
{
    return __mask_signal(sig, SIG_UNBLOCK);
}

/* Install signal handler for signal 'sig' */
void
signal_set_handler(int sig, sa_sigaction_t handler)
{
    sigset_t emptymask;
    if (sig != SIGCHLD) {
        fprintf(stderr,
            "For the cush project, the only signal you need to catch\n"
            "is SIGCHLD.  In particular, implementing Ctrl-Z and Ctrl-C\n"
            "for child process does not require signal handling on the\n"
            "part of the shell.\n"
            "The only reason to handle SIGINT and/or SIGTSTP by the shell\n"
            "itself is if you wanted to prevent a user from using Ctrl-Z/C\n"
            "from affecting the shell itself while it is sitting at the prompt\n"
            "Remove this statement if you know what you're doing\n"
        );
        abort();
    }
    sigemptyset(&emptymask);
    struct sigaction sa = {
        .sa_sigaction = handler,
        /* do not block any additional signals (besides 'sig') when
         * signal handler is entered. */
        .sa_mask = emptymask,
        /* restart system calls when possible */
        .sa_flags = SA_RESTART | SA_SIGINFO
    };

    if (sigaction(sig, &sa, NULL) != 0)
        utils_fatal_error("sigaction failed for signal %d", sig);
}
