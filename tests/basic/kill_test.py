#!/usr/bin/python
#
# kill_test: tests the kill command with the default
# semantics of:
#
# kill <jid>
#
# This test may require updating such that we test other signals
# 
# Requires the following commands to be implemented
# or otherwise usable:
#
#	kill, sleep
#

import sys, imp, atexit
import pexpect, shellio, signal, time, os, re, proc_check


#Ensure the shell process is terminated
def force_shell_termination(shell_process):
	c.close(force=True)

# pulling in the regular expression and other definitions
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

# ensure that the shell prints the expected prompt
assert c.expect(def_module.prompt) == 0, "Shell did not print expected prompt"



# run a command
c.sendline("sleep 30 &")

# parse the jobid and pid output
(jobid, pid) = shellio.parse_regular_expression(c, def_module.bgjob_regex)

# ensure that the shell prints the expected prompt
assert c.expect(def_module.prompt) == 0, "Shell did not print expected prompt"

# The job needs to be running when we call kill
proc_check.count_children_timeout(c, 1, 1)

# Run the kill command and kill the sleep process in the background
c.sendline(def_module.builtin_commands['kill'] % jobid)

# ensure that the shell prints the expected prompt
assert c.expect(def_module.prompt) == 0, "Shell did not print expected prompt"

# ensure there is enough time for the process to be killed
time.sleep(.5)



# check the proc file that the process has actually been stopped
# the proc file should not exist
assert not os.path.exists("/proc/" + pid + "/stat"), 'the process was not \
killed'


# end the shell program by sending it an end-of-file character
c.sendline("exit");

# ensure that no extra characters are output after exiting
assert c.expect_exact("exit\r\n") == 0, "Shell output extraneous characters"


# the test was successful
shellio.success()
