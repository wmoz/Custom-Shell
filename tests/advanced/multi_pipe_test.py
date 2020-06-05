#!/usr/bin/python
#
# Block header comment
#
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

#spawn an instance of the shell
c = pexpect.spawn(def_module.shell, drainpty=True, logfile=logfile)
atexit.register(force_shell_termination, shell_process=c)


# Test a really long pipeline
# Cat acts as the identity function for pipes

c.sendline("echo hi | cat | cat | cat | cat | cat | cat | cat")
assert c.expect_exact("hi\r\n") == 0, "multiple cats didn't work"

assert c.expect(def_module.prompt) == 0, "Shell did not print expected prompt"

# Test multiple substitutions through pipes

c.sendline("echo hello how are you | sed s/how/who/ | sed s/are/am/ | sed s/you/I/")
assert c.expect_exact("hello who am I\r\n") == 0, "Sed didn't work"

assert c.expect(def_module.prompt) == 0, "Shell did not print expected prompt"

# Reverse a string twice to get back the original
c.sendline("echo string | rev | rev ")
assert c.expect_exact("string\r\n") == 0, "Reverse twice didn't return string"

assert c.expect(def_module.prompt) == 0, "Shell did not print expected prompt"

shellio.success()
