#!/usr/bin/python
#
# reap_on_child_termination_test: 
# Tests that a background process will be reaped when it finishes
# even when the shell is waiting for user input with readline. 
# This requires the SIGCHLD to not be blocked when waiting on user input
# We want to have this behaviour so that the process resources are not tied up unnecessarily.
# 
#
#
# 
# Requires the following commands to be implemented
# or otherwise usable:
#
#	sleep
#
# In addition the shell must be capable of running processes in the background.
# This includes giving the proper output when running a command in the background
# this is of the form:
#
# [jobid] pid
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



# run a sleep command for enough time to allow the prompt to return to the shell
c.sendline("sleep 3 &")

# parse the jobid and pid output
(jobid, pid) = shellio.parse_regular_expression(c, def_module.bgjob_regex)

# ensure that the shell prints the expected prompt within a reasonable time
assert c.expect(def_module.prompt) == 0, "Shell did not print expected prompt"

# The job needs to be running when it prints the prompt so that when it dies we can see if it reaps it
# If it takes more than 3 seconds to give the prompt back to the user after typing in sleep 3 & then 
# the shell should be deemed much too slow anyways and will fail the test
# we do not expect 3 seconds to be a time student shells will come close to at all.
proc_check.count_children_timeout(c, 1, 1)


# sleep for enough time to ensure that the sleep program has terminated and should have been reaped
time.sleep(4.5)

# check the proc file that the process has actually been reaped
# the proc file should not exist once it has been reaped
# Note that this is checking for the existence of the /proc file
# outside of the shell that spawned the command. This can be simulated by students
# by opening up another shell and looking at /proc
assert not os.path.exists("/proc/" + pid + "/stat"), 'the process was not \
reaped'


# end the shell program by sending it an end-of-file character
c.sendline("exit");

# ensure that no extra characters are output after exiting
assert c.expect_exact("exit\r\n") == 0, "Shell output extraneous characters"


# the test was successful
shellio.success()
