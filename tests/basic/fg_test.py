#!/usr/bin/python
#
# fg_test: tests the fg command
# 
# Test the fg command for bringing a command back to the foreground.
# Requires the following commands to be implemented
# or otherwise usable:
#
#	fg, sleep, ctrl-c control, ctrl-z control
#

import sys, imp, atexit
import pexpect, shellio, signal, time, os, re, proc_check

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
assert c.expect(def_module.prompt) == 0, "Shell did not print expected prompt (1)"


# run a command
c.sendline("sleep 60")

# The following call is necessary to ensure that the SIGTSTP
# we are sending below via 'sendcontrol' reaches the 'sleep' child.
proc_check.wait_until_child_is_in_foreground(c)

# send SIGTSTP to 'sleep'
c.sendcontrol('z')

# shell should pick up that 'sleep' was stopped and respond with job status
# it should output a line such [6]+  Stopped                 sleep 60
(jobid, statusmsg, cmdline) = \
        shellio.parse_regular_expression(c, def_module.job_status_regex)
assert statusmsg == def_module.jobs_status_msg['stopped'], "Shell did not report stopped job"

# move job into foreground
c.sendline(def_module.builtin_commands['fg'] % jobid)

# when moving a job in the foreground, bash outputs its command line
assert c.expect_exact(cmdline) == 0, "Shell did not report the job moved into the foreground"

# end the program
c.sendintr()



# run a command
c.sendline("sleep 30 &")

#snag the jobid and pid of the sleep command
(jobid, pid) = shellio.parse_regular_expression(c, def_module.bgjob_regex)

#check the prompt prints
assert c.expect(def_module.prompt) == 0, "Shell did not print expected prompt (2)"



#resume the sleep command
c.sendline(def_module.builtin_commands['fg'] % jobid)

#wait until it takes over the foreground process group
proc_check.wait_until_child_is_in_foreground(c)

#send the command back to the background
c.sendcontrol('z')

#check the prompt prints
assert c.expect(def_module.prompt) == 0, "Shell did not print expected prompt (3)"



#run a command to the background
c.sendline("sleep 300 &")

#snag the jobid and pid of the second sleep command
(jobid2, pid2) = shellio.parse_regular_expression(c, def_module.bgjob_regex)

#check the prompt prints
assert c.expect(def_module.prompt) == 0, "Shell did not print expected prompt (4)"

#resume the command by its jobid
c.sendline(def_module.builtin_commands['fg'] % jobid)

#wait until it takes over the foreground process group
proc_check.wait_until_child_is_in_foreground(c)

#Ensure that the sleep is in the foreground process group via /proc/
assert proc_check.check_pid_fgpgrp(pid),  "Error, the pid's process group is \
                                           not the foreground process group"

#send the command back to the background
c.sendcontrol('z')

#check the prompt prints
assert c.expect(def_module.prompt) == 0, "Shell did not print expected prompt (5)"



#resume the command by its jobid
c.sendline(def_module.builtin_commands['fg'] % jobid2)

#wait until it takes over the foreground process group
proc_check.wait_until_child_is_in_foreground(c)

#Ensure that the sleep is in the foreground process group via /proc/
assert proc_check.check_pid_fgpgrp(pid2),  "Error, the pid's process group is \
                                           not the foreground process group"

#end the process
c.sendintr()

#check that the prompt prints
assert c.expect(def_module.prompt) == 0, "Shell did not print expected prompt (6)"



#resume the first sleep command
c.sendline(def_module.builtin_commands['fg'] % jobid)

#wait until the process takes over the foreground process group
proc_check.wait_until_child_is_in_foreground(c)

#Ensure that the sleep is in the foreground process group via /proc/
assert proc_check.check_pid_fgpgrp(pid),  "Error, the pid's process group is \
                                           not the foreground process group"



#exit
c.sendline("exit");
assert c.expect_exact("exit\r\n") == 0, "Shell output extraneous characters"


shellio.success()
