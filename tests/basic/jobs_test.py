#!/usr/bin/python
#
# jobs_test: tests the jobs command
# 
# Test the jobs command for status messages and proper output
# of the command and jobid.  Requires the following commands to be implemented
# or otherwise usable:
#
#	jobs, ctrl-z control, ctrl-c control, sleep, fg, clear
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

# spawn an instance of bash.  PS1 is the env variable from which bash
# draws its prompt
c = pexpect.spawn(def_module.shell, drainpty=True, logfile=logfile)
atexit.register(force_shell_termination, shell_process=c)

# set timeout for all following 'expect*' calls to 2 seconds
c.timeout = 2

# ensure that shell prints expected prompt
assert c.expect(def_module.prompt) == 0, "Shell did not print expected prompt"



#check that the jobs list outputs nothing
c.sendline(def_module.builtin_commands['jobs'])

# ensure that shell prints expected prompt
assert c.expect(def_module.prompt) == 0, "Shell did not print expected prompt"



# run a command
c.sendline("sleep 30")

#Wait until the sleep command takes over the foreground
proc_check.wait_until_child_is_in_foreground(c)

#send the job to the background
c.sendcontrol('z')

# ensure that the shell prints the expected prompt
assert c.expect(def_module.prompt) == 0, "Shell did not print expected prompt"



#Request the jobs list
c.sendline(def_module.builtin_commands['jobs'])

#Check the jobs list
(jobid, status_message, command_line) = shellio.parse_regular_expression(c, def_module.job_status_regex)
assert status_message == def_module.jobs_status_msg['stopped'] and \
		'sleep 30' in command_line, "Job status not properly displayed"

# ensure that the shell prints the expected prompt
assert c.expect(def_module.prompt) == 0, "Shell did not print expected prompt"



#Add another job
c.sendline("sleep 300 &")

# pick up the background job output
(jobid, pid) = shellio.parse_regular_expression(c, def_module.bgjob_regex)

assert c.expect(def_module.prompt) == 0, "Shell did not print expected prompt"

#Both jobs need to be active and running before the jobs command is
#sent.  if this isn't so, the test is failed.
proc_check.count_active_children(c, 2)



#Recheck the jobs list
c.sendline(def_module.builtin_commands['jobs'])

#Check the jobs list
(jobid, status_message, command_line) = \
            shellio.parse_regular_expression(c, def_module.job_status_regex) 
(jobid2, status_message2, command_line2) = \
            shellio.parse_regular_expression(c, def_module.job_status_regex)

# Check that the jobs list contains both jobs in some order

#check the first possible order of job statuses, and then
#the second possible order.
assert  (status_message == def_module.jobs_status_msg['stopped'] and \
		'sleep 30' in command_line and \
		\
		status_message2 == def_module.jobs_status_msg['running'] and \
		'sleep 300' in command_line2) \
		\
		or \
		\
		(status_message2 == def_module.jobs_status_msg['stopped'] and \
		'sleep 30' in command_line2 and \
		\
		status_message == def_module.jobs_status_msg['running'] and \
		'sleep 300' in command_line), "Job status not properly displayed"

# Check that there are no duplicate job id's.
assert jobid != jobid2, "Duplicate job id's."

assert c.expect(def_module.prompt) == 0, "Shell did not print expected prompt"



#bring the second sleep command back to foreground 
#so that we can end it with ctrl-c
c.sendline(def_module.builtin_commands['fg'] % jobid2)

#Wait until the sleep command takes over the foreground
proc_check.wait_until_child_is_in_foreground(c)

#ctrl-c to close the process
c.sendintr()



#clear any printout of the old job that was just killed by ctrl-c
c.sendline(def_module.builtin_commands['jobs'])

#check the prompt and move past this text
assert c.expect(def_module.prompt) == 0, "Shell did not print expected prompt"


#check the prompt and move past this text
assert c.expect(def_module.prompt) == 0, "Shell did not print expected prompt"


#check the jobs list
c.sendline(def_module.builtin_commands['jobs'])

#check that the first job is still on the jobs list
assert (jobid, status_message, command_line) == \
    shellio.parse_regular_expression(c, def_module.job_status_regex), \
    "The original job was not displayed properly after ending a previous job."

# ensure the prompt is printed
assert c.expect(def_module.prompt) == 0, "Shell did not print expected prompt"



# exit
c.sendline("exit");
assert c.expect_exact("exit\r\n") == 0, "Shell output extraneous characters"


shellio.success()
