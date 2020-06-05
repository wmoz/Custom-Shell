#!/usr/bin/python
#
# Ctrl-C Test: Start a shell, send SIGINT, run a program, 
#              send SIGINT, then exit
#
# Requires the following commands to be implemented
# or otherwise usable:
#
#	sleep, ctrl-c control
#

import sys, imp, atexit
import pexpect, proc_check, shellio, signal, time, threading

#Ensure the shell process is terminated
def force_shell_termination(shell_process):
	c.close(force=True)

#pulling in the regular expression and other definitions
definitions_scriptname = sys.argv[1]
def_module = imp.load_source('', definitions_scriptname)
logfile = None
if hasattr(def_module, 'logfile'):
    logfile = def_module.logfile

# spawn an instance of the shell
c = pexpect.spawn(def_module.shell, drainpty=True, logfile=logfile)
atexit.register(force_shell_termination, shell_process=c)

# set timeout for all following 'expect*' calls to 2 seconds
c.timeout = 2

# ensure that shell prints expected prompt
assert c.expect(def_module.prompt) == 0, "Shell did not print expected prompt"

# run a command
c.sendline("sleep 60")

# The following call is necessary to ensure that the SIGINT
# we are sending below reaches the 'sleep' child.
proc_check.wait_until_child_is_in_foreground(c)

#checks that our process is running
proc_check.count_active_children(c, 1)

# send SIGINT
c.sendintr()

#prompt check
assert c.expect(def_module.prompt) == 0, "Shell did not print expected prompt"

#checks that the process was ended
proc_check.count_active_children(c, 0)



c.sendline("exit")

assert c.expect_exact("exit\r\n") == 0, "Shell output extraneous characters"


shellio.success()
