"""
Utility module for advanced tests.
"""
import sys, imp, atexit
import os, re, time, pexpect, shellio, tempfile, proc_check, shutil, stat, signal

from collections import namedtuple

console = None
settings_module = None

def setup_tests():
    global console
    global settings_module
    
    definitions_scriptname = sys.argv[1]
    settings_module = imp.load_source('', definitions_scriptname)
    logfile = None
    if hasattr(settings_module, 'logfile'):
        logfile = settings_module.logfile

    #spawn an instance of the shell
    console = pexpect.spawn(settings_module.shell, drainpty=True, logfile=logfile or sys.stdout)
    atexit.register(kill, shell_process=console)

    # set timeout for all following 'expect*' calls to 2 seconds
    console.timeout = 2 


def sendline(line):
    console.sendline(line)

def sendcontrol(ch):
    console.sendcontrol(ch)

test_success = shellio.success

def expect(line, message=None):
    if message is None:
        message = 'expected "{}" but did not find'

    assert console.expect(line) == 0, message

def expect_exact(line, message=None):
    if message is None:
        message = 'expected "{}" but did not find'

    assert console.expect_exact(line) == 0, message

def expect_prompt(message=None):
    if message is None:
        message = 'shell did not return to prompt'

    assert console.expect(settings_module.prompt) == 0, message

def expect_regex(regex):
    return shellio.parse_regular_expression(console, regex)

def kill(shell_process):
    console.close(force=True)

def wait_for_fg_child():
    proc_check.wait_until_child_is_in_foreground(console)

def parse_job_line():
    job_status = namedtuple('job_status', ['id', 'status', 'command'])

    jid, status, cmd = expect_regex(settings_module.job_status_regex)
    for name, val in settings_module.jobs_status_msg.items():
        if val == status:
            status = name
            break
    
    return job_status(jid, status, cmd)

def parse_bg_status():
    bg_status = namedtuple('bg_status', ['job_id', 'pid'])
    jid, pid = expect_regex(settings_module.bgjob_regex)
    return bg_status(jid, pid)

def run_builtin(command, *args):
    command = settings_module.builtin_commands.get(command, command)
    sendline(command % tuple(args))

def assert_correct_fds(pid, message):
    '''Checks that file descriptors are not leaked into
    the child. An allowance is given for fd 3 if it is a tty
    because the original example code leaks but this is
    fixed upstream.'''

    time.sleep(0.5)
    fds = sorted(os.listdir('/proc/{0}/fd'.format(pid)))
    tty = True
    print fds
    if '3' in fds:
        tty = os.path.samefile('/proc/{0}/fd/3'.format(pid), '/dev/tty')

    if not (fds == list('012') or (fds == list('0123') and tty)):
        raise Exception('File descriptors leaked into child! Remember to close() all of the pipes and IO redir file descriptors')

def get_shell_pid():
    return console.pid
