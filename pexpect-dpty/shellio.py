#####
#
# Collection of handy routines for testing shells
#
# Below, adjust the regular expressions as necessary to match
# the output of your shell.  The expressions shown conform to
# the output of the bash shell.
#
# Include this file with "import shellio"
#
#####
import sys
import os, re, pexpect, time, traceback

def success(msg = "", exit = True):
    """Print PASS message and optionally 'msg'.  Exit unless 'exit is False"""
    print "PASS", msg
    if exit:
        sys.exit(0)

def parse_regular_expression(pexpect_child, regex):
    """
    A regular expression was printed and is matched into groups and returned.
    The return value will be a tuple of all of the parenthetical groups found
    in the regular expression.  For example:
    regex = \[(\d+)\] (\d+) will return a tuple
    of the form (decimal_one, decimal_two) as it was read from the output
    of the shell "[decimal_one] decimal_two".
    """
    pexpect_child.expect(regex)
    return __regex2tuple(pexpect_child.match)

#
# Attention: parse_background_job and parse_job_status are not used.
# We use output_spec.py regexp currently.
#
BACKGROUND_JOB_MESSAGE = "\[(\d+)\] (\d+)"
def parse_background_job(pexpect_child):
    """
    # a message that job was sent to the background, e.g., 
    # [1] 2273
    #
    # Returns tuple (jobid, processid)
    #
    """
    pexpect_child.expect(BACKGROUND_JOB_MESSAGE)
    return __regex2tuple(pexpect_child.match)

JOB_STATUS_MESSAGE = "\[(\d+)\].?\s+(\S+)\s+(.+?)\r\n"
def parse_job_status(pexpect_child):
    """
    # a message is printed about a job status
    # [4]   Running                 sleep 100 &
    # [5]+  Stopped                 sleep 10
    # [6]+  Stopped                 sleep 10
    # Returns (jobid, status_message, commandline)
    """
    pexpect_child.expect(JOB_STATUS_MESSAGE)
    return __regex2tuple(pexpect_child.match)

def __regex2tuple(match):
    """Turn a matched regular expression into a tuple of its captured groups"""
    return tuple([ match.group(i) for i in range(1, match.lastindex + 1) ])

def handle_exception(type, exc, tb):
    """Install a default exception handler.
    If there is a pexpect.TIMEOUT exception thrown at any time in the script,
    report that the test failed because of a timeout and exit.
    """
    if type == pexpect.TIMEOUT:
        print "\n>>> FAIL: Test timed out", exc.get_trace(), "\n"
    else:
        print "\n>>> FAIL: ", type, "'", exc, "'\n"
    traceback.print_tb(tb)

sys.excepthook = handle_exception
